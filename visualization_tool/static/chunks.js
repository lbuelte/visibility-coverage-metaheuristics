"use strict";


/*
 * Chunked map data.
 *
 * During preprocessing the server splits every layer into fixed spatial
 * chunks of limited size. For a bounding box, the server lists the
 * intersecting chunks, nearest to the map centre first. Every chunk is
 * then fetched as its own small, immutable, browser-cacheable request.
 *
 * - ChunkStore keeps parsed chunks in memory (least recently used first
 *   out). Evicted chunks usually come back from the HTTP cache quickly.
 * - ChunkLoader fetches chunks with a priority queue. Foreground requests
 *   (chunks needed for the current view) always go first; background
 *   requests (preloading) only use a free slot when no foreground request
 *   is waiting. Requests in flight are never aborted: chunks are small, and
 *   the result is useful for the store either way.
 * - ChunkBuildQueue turns chunks into Leaflet layers one at a time, in small
 *   time slices, so that the map stays responsive.
 * - ChunkedLayer is one map layer: a Leaflet layer group with one child
 *   layer per chunk. It attaches the chunks of the current view, detaches
 *   chunks far outside it, and keeps a limited number of detached layers
 *   for a quick return.
 */


const CHUNK_FOREGROUND_CONCURRENCY = 4;

/*
 * Browsers allow six parallel HTTP/1.1 connections per host. Four
 * foreground and one background request leave one connection free for
 * index, statistics, and extent requests.
 */
const CHUNK_BACKGROUND_CONCURRENCY = 1;

/* Upper bound for parsed chunks kept in memory, in uncompressed bytes. */
const CHUNK_STORE_MAX_BYTES = 150 * 1024 * 1024;

/* Detached Leaflet layers kept per map layer for a quick return. */
const DETACHED_LAYER_MAX_FEATURES = 60000;

/* Time per animation frame spent on building Leaflet layers. */
const BUILD_FRAME_BUDGET_MS = 8;
const BUILD_BATCH_SIZE = 100;


function createAbortError() {
    const error = new Error("Request was aborted.");
    error.name = "AbortError";

    return error;
}


function nextRenderOpportunity() {
    return new Promise(resolve => {
        if (document.hidden) {
            window.setTimeout(resolve, 0);
            return;
        }

        window.requestAnimationFrame(() => resolve());
    });
}


function chunkBboxesOverlap(first, second) {
    if (!first || !second) {
        return false;
    }

    return !(
        first[2] < second[0] ||
        first[0] > second[2] ||
        first[3] < second[1] ||
        first[1] > second[3]
    );
}


class ChunkRequestError extends Error {
    constructor(message, status) {
        super(message);

        this.name = "ChunkRequestError";
        this.status = status;
    }
}


class ChunkStore {
    constructor(maxBytes) {
        this.maxBytes = maxBytes;
        this.entries = new Map();
        this.totalBytes = 0;
    }

    has(url) {
        return this.entries.has(url);
    }

    get(url) {
        const entry = this.entries.get(url);

        if (!entry) {
            return null;
        }

        /* Re-inserting moves the entry to the most recently used end. */
        this.entries.delete(url);
        this.entries.set(url, entry);

        return entry.payload;
    }

    put(url, payload, bytes) {
        if (this.entries.has(url)) {
            this.get(url);
            return;
        }

        this.entries.set(url, {payload, bytes});
        this.totalBytes += bytes;

        for (const [storedUrl, entry] of this.entries) {
            if (this.totalBytes <= this.maxBytes) {
                break;
            }

            this.entries.delete(storedUrl);
            this.totalBytes -= entry.bytes;
        }
    }
}


class ChunkLoader {
    constructor(store) {
        this.store = store;

        /* url -> item; an item collects all owners waiting for a chunk. */
        this.queued = new Map();
        this.inFlight = new Map();

        this.activeForeground = 0;
        this.activeBackground = 0;

        this.listeners = new Set();
    }

    onChange(listener) {
        this.listeners.add(listener);
    }

    notify() {
        for (const listener of this.listeners) {
            listener();
        }
    }

    /*
     * Return a promise for the parsed chunk. Each owner (a map layer or a
     * preloading task) has at most one request per chunk; a newer request
     * replaces the owner's priority.
     */
    request(url, {owner, foreground, order, bytes}) {
        const stored = this.store.get(url);

        if (stored) {
            return Promise.resolve(stored);
        }

        let item = (
            this.inFlight.get(url) ||
            this.queued.get(url)
        );

        if (!item) {
            item = {
                url,
                bytes: Number(bytes) || 0,
                owners: new Map(),
                waiters: []
            };

            this.queued.set(url, item);
        }

        item.owners.set(owner, {foreground, order});

        const promise = new Promise((resolve, reject) => {
            item.waiters.push({owner, resolve, reject});
        });

        this.pump();
        this.notify();

        return promise;
    }

    /*
     * Withdraw all requests of an owner. Queued chunks nobody else waits
     * for are dropped; chunks in flight finish and go to the store.
     */
    cancelOwner(owner) {
        let changed = false;

        for (const items of [this.queued, this.inFlight]) {
            for (const [url, item] of items) {
                if (!item.owners.delete(owner)) {
                    continue;
                }

                changed = true;

                const remaining = [];

                for (const waiter of item.waiters) {
                    if (waiter.owner === owner) {
                        waiter.reject(createAbortError());
                    } else {
                        remaining.push(waiter);
                    }
                }

                item.waiters = remaining;

                if (
                    items === this.queued &&
                    item.owners.size === 0
                ) {
                    this.queued.delete(url);
                }
            }
        }

        if (changed) {
            this.pump();
            this.notify();
        }
    }

    static isForeground(item) {
        for (const owner of item.owners.values()) {
            if (owner.foreground) {
                return true;
            }
        }

        return false;
    }

    static priority(item) {
        let best = Number.POSITIVE_INFINITY;

        for (const owner of item.owners.values()) {
            best = Math.min(best, owner.order);
        }

        return best;
    }

    nextItem(foreground) {
        let selected = null;
        let selectedPriority = Number.POSITIVE_INFINITY;

        for (const item of this.queued.values()) {
            if (ChunkLoader.isForeground(item) !== foreground) {
                continue;
            }

            const priority = ChunkLoader.priority(item);

            if (selected === null || priority < selectedPriority) {
                selected = item;
                selectedPriority = priority;
            }
        }

        return selected;
    }

    hasQueuedForeground() {
        for (const item of this.queued.values()) {
            if (ChunkLoader.isForeground(item)) {
                return true;
            }
        }

        return false;
    }

    pump() {
        while (
            this.activeForeground < CHUNK_FOREGROUND_CONCURRENCY
        ) {
            const item = this.nextItem(true);

            if (item === null) {
                break;
            }

            this.start(item, true);
        }

        if (this.hasQueuedForeground()) {
            return;
        }

        while (
            this.activeBackground < CHUNK_BACKGROUND_CONCURRENCY
        ) {
            const item = this.nextItem(false);

            if (item === null) {
                break;
            }

            this.start(item, false);
        }
    }

    async start(item, foregroundSlot) {
        this.queued.delete(item.url);
        this.inFlight.set(item.url, item);

        if (foregroundSlot) {
            this.activeForeground += 1;
        } else {
            this.activeBackground += 1;
        }

        try {
            const payload = await this.fetchChunk(item.url);

            this.store.put(item.url, payload, item.bytes);

            for (const waiter of item.waiters) {
                waiter.resolve(payload);
            }
        } catch (error) {
            for (const waiter of item.waiters) {
                waiter.reject(error);
            }
        } finally {
            item.waiters = [];
            this.inFlight.delete(item.url);

            if (foregroundSlot) {
                this.activeForeground -= 1;
            } else {
                this.activeBackground -= 1;
            }

            this.pump();
            this.notify();
        }
    }

    async fetchChunk(url) {
        let response;

        try {
            response = await fetch(url, {
                headers: {
                    "Accept": "application/geo+json, application/json"
                }
            });
        } catch (error) {
            throw new ChunkRequestError(
                "Could not connect to the server: " +
                `${error?.message || "network error"}`,
                0
            );
        }

        if (!response.ok) {
            throw new ChunkRequestError(
                `A map data chunk request failed with HTTP ` +
                `${response.status}.`,
                response.status
            );
        }

        const payload = await response.json();

        if (
            !payload ||
            payload.type !== "FeatureCollection" ||
            !Array.isArray(payload.features)
        ) {
            throw new ChunkRequestError(
                "A map data chunk is not a valid GeoJSON " +
                "FeatureCollection.",
                response.status
            );
        }

        return payload;
    }
}


class ChunkBuildQueue {
    constructor() {
        this.tasks = [];
        this.sequence = 0;
        this.running = false;
    }

    add(order, run) {
        this.tasks.push({
            order,
            sequence: this.sequence,
            run
        });

        this.sequence += 1;

        if (!this.running) {
            this.drain();
        }
    }

    async drain() {
        this.running = true;

        try {
            while (this.tasks.length > 0) {
                let selectedIndex = 0;

                for (
                    let index = 1;
                    index < this.tasks.length;
                    index += 1
                ) {
                    const task = this.tasks[index];
                    const selected = this.tasks[selectedIndex];

                    if (
                        task.order < selected.order ||
                        (
                            task.order === selected.order &&
                            task.sequence < selected.sequence
                        )
                    ) {
                        selectedIndex = index;
                    }
                }

                const [task] = this.tasks.splice(selectedIndex, 1);

                try {
                    await task.run();
                } catch (error) {
                    console.error("Could not build a map layer:", error);
                }
            }
        } finally {
            this.running = false;
        }
    }
}


async function addFeaturesInSlices(layer, features, isCurrent) {
    let frameStart = performance.now();

    for (
        let start = 0;
        start < features.length;
        start += BUILD_BATCH_SIZE
    ) {
        if (!isCurrent()) {
            throw createAbortError();
        }

        layer.addData(
            features.slice(start, start + BUILD_BATCH_SIZE)
        );

        if (performance.now() - frameStart > BUILD_FRAME_BUDGET_MS) {
            await nextRenderOpportunity();
            frameStart = performance.now();
        }
    }
}


class ChunkedLayer {
    /*
     * owner: unique name used for the loader queue.
     * createLayer: returns an empty Leaflet layer with an addData method.
     * onAttach: optional; called with a chunk's Leaflet layer whenever it
     *     is attached to the map.
     * onChange: called whenever the loading state may have changed.
     * onError: called for failed chunks (not for cancellations).
     */
    constructor({owner, loader, store, buildQueue, createLayer, onAttach, onChange, onError}) {
        this.owner = owner;
        this.loader = loader;
        this.store = store;
        this.buildQueue = buildQueue;
        this.createLayer = createLayer;
        this.onAttach = onAttach || null;
        this.onChange = onChange;
        this.onError = onError;

        this.group = L.layerGroup();

        /* url -> record; one record per known chunk. */
        this.records = new Map();
        /* url -> descriptor of the chunks the current view needs. */
        this.wanted = new Map();

        this.version = null;
        this.generation = 0;
        this.detachCounter = 0;
    }

    /*
     * Show the given chunks and fetch missing ones in the given order.
     * Attached chunks that are no longer needed stay attached while they
     * overlap retainBbox, so small pans do not detach and re-attach them.
     */
    sync(descriptors, retainBbox, orderBias = 0, version = null) {
        if (version !== null && version !== this.version) {
            /* The cache was rebuilt: old chunks must not stay visible. */
            this.clear();
            this.version = version;
        }

        this.wanted = new Map(
            descriptors.map(descriptor => [descriptor.url, descriptor])
        );

        /* Drop queued requests the view no longer needs. */
        this.loader.cancelOwner(this.owner);

        for (const descriptor of descriptors) {
            const record = this.recordFor(descriptor);

            record.failed = false;

            if (record.layer) {
                this.attach(record);
                continue;
            }

            this.ensureLoaded(record, orderBias + descriptor.order);
        }

        for (const record of this.records.values()) {
            if (
                record.attached &&
                !this.wanted.has(record.url) &&
                !chunkBboxesOverlap(record.bbox, retainBbox)
            ) {
                this.detach(record);
            }
        }

        this.trimDetached();
        this.onChange();
    }

    /* Detach everything and stop loading, keeping built layers. */
    release() {
        if (
            this.wanted.size === 0 &&
            this.group.getLayers().length === 0
        ) {
            return;
        }

        this.sync([], null);
    }

    /* Forget all chunks, for example after a selection change. */
    clear() {
        this.generation += 1;
        this.version = null;

        this.loader.cancelOwner(this.owner);
        this.group.clearLayers();

        this.records.clear();
        this.wanted = new Map();

        this.onChange();
    }

    isLoading() {
        for (const url of this.wanted.keys()) {
            const record = this.records.get(url);

            if (record && !record.attached && !record.failed) {
                return true;
            }
        }

        return false;
    }

    recordFor(descriptor) {
        let record = this.records.get(descriptor.url);

        if (!record) {
            record = {
                url: descriptor.url,
                bbox: descriptor.bbox,
                bytes: descriptor.bytes,
                featureCount: descriptor.count,
                layer: null,
                attached: false,
                building: false,
                failed: false,
                requestToken: null,
                detachedAt: 0
            };

            this.records.set(descriptor.url, record);
        }

        return record;
    }

    ensureLoaded(record, order) {
        if (record.building) {
            return;
        }

        const stored = this.store.get(record.url);

        if (stored) {
            this.scheduleBuild(record, stored, order);
            return;
        }

        const generation = this.generation;
        const token = {};

        record.requestToken = token;

        this.loader.request(record.url, {
            owner: this.owner,
            foreground: true,
            order,
            bytes: record.bytes
        }).then(payload => {
            if (record.requestToken !== token) {
                return;
            }

            record.requestToken = null;

            if (
                generation === this.generation &&
                this.wanted.has(record.url)
            ) {
                this.scheduleBuild(record, payload, order);
            }

            this.onChange();
        }).catch(error => {
            if (record.requestToken !== token) {
                return;
            }

            record.requestToken = null;

            if (
                error?.name !== "AbortError" &&
                generation === this.generation
            ) {
                record.failed = true;
                this.onError(error);
            }

            this.onChange();
        });
    }

    scheduleBuild(record, payload, order) {
        if (record.building || record.layer) {
            return;
        }

        const generation = this.generation;
        const isCurrent = () => (
            generation === this.generation &&
            this.wanted.has(record.url)
        );

        record.building = true;

        this.buildQueue.add(order, async () => {
            try {
                if (!isCurrent()) {
                    return;
                }

                const layer = this.createLayer();

                await addFeaturesInSlices(
                    layer,
                    payload.features,
                    isCurrent
                );

                record.layer = layer;
                record.featureCount = payload.features.length;
                this.attach(record);
            } catch (error) {
                if (error?.name !== "AbortError") {
                    record.failed = true;
                    this.onError(error);
                }
            } finally {
                record.building = false;
                this.onChange();
            }
        });
    }

    attach(record) {
        if (record.attached || !record.layer) {
            return;
        }

        if (this.onAttach) {
            this.onAttach(record.layer);
        }

        this.group.addLayer(record.layer);
        record.attached = true;
    }

    forEachAttachedLayer(callback) {
        for (const record of this.records.values()) {
            if (record.attached) {
                callback(record.layer);
            }
        }
    }

    detach(record) {
        if (!record.attached) {
            return;
        }

        this.group.removeLayer(record.layer);
        record.attached = false;

        this.detachCounter += 1;
        record.detachedAt = this.detachCounter;
    }

    trimDetached() {
        const detached = [];
        let detachedFeatures = 0;

        for (const record of this.records.values()) {
            if (record.layer && !record.attached) {
                detached.push(record);
                detachedFeatures += record.featureCount;
            }
        }

        if (detachedFeatures <= DETACHED_LAYER_MAX_FEATURES) {
            return;
        }

        detached.sort(
            (first, second) => first.detachedAt - second.detachedAt
        );

        for (const record of detached) {
            if (detachedFeatures <= DETACHED_LAYER_MAX_FEATURES) {
                break;
            }

            record.layer = null;
            detachedFeatures -= record.featureCount;
        }
    }
}
