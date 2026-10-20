"use strict";


const sourceSelect = document.getElementById("sourceSelect");
const algorithmSelect = document.getElementById("algorithmSelect");
const kSelect = document.getElementById("kSelect");
const tauSelect = document.getElementById("tauSelect");

const instanceInformation = document.getElementById(
    "instanceInformation"
);
const instanceInformationToggle = document.getElementById(
    "instanceInformationToggle"
);
const instanceInformationTitle = document.getElementById(
    "instanceInformationTitle"
);
const instanceInformationSummary = document.getElementById(
    "instanceInformationSummary"
);
const instanceSourceName = document.getElementById(
    "instanceSourceName"
);
const instancePolygonCount = document.getElementById(
    "instancePolygonCount"
);
const solutionStatisticsTable = document.getElementById(
    "solutionStatisticsTable"
);
const statisticsFootnote = document.getElementById(
    "statisticsFootnote"
);

const visibilityToggle = document.getElementById("visibilityToggle");
const overlayToggle = document.getElementById("overlayToggle");
const overlayPanel = document.getElementById("overlayPanel");
const closeOverlayButton = document.getElementById(
    "closeOverlayButton"
);

const overlayAlgorithmSelect = document.getElementById(
    "overlayAlgorithmSelect"
);
const overlayKSelect = document.getElementById(
    "overlayKSelect"
);
const overlayTauSelect = document.getElementById(
    "overlayTauSelect"
);

const solutionFade = document.getElementById("solutionFade");
const fadeOutput = document.getElementById("fadeOutput");

const backgroundOpacity = document.getElementById(
    "backgroundOpacity"
);
const backgroundOpacityOutput = document.getElementById(
    "backgroundOpacityOutput"
);

const applicationTitle = document.getElementById("applicationTitle");
const statusElement = document.getElementById("status");
const statusTextElement = document.getElementById("statusText");
const mapShell = document.querySelector(".map-shell");


const PRIMARY_THEME = {
    name: "Primary",
    covered: "#168548",
    coveredFill: "#39ad6a",
    uncovered: "#bd363b",
    uncoveredFill: "#df5a5e",
    edge: "#149447",
    visibility: "#3478a5",
    visibilityFill: "#65a9d2"
};

const COMPARISON_THEME = {
    name: "Comparison",
    covered: "#0879bd",
    coveredFill: "#29a5db",
    uncovered: "#dd7417",
    uncoveredFill: "#f5a13f",
    edge: "#7c3aed",
    visibility: "#9a42d1",
    visibilityFill: "#c16ee8"
};

const BASE_LAYER_NAMES = [
    "antennas",
    "covered",
    "uncovered",
    "edges"
];

const AUXILIARY_LAYER_NAMES = [
    "labels",
    "visibility"
];

const ALL_SOLUTION_LAYER_NAMES = [
    ...BASE_LAYER_NAMES,
    ...AUXILIARY_LAYER_NAMES
];

const API_LAYER_NAMES = {
    covered: "covered",
    uncovered: "uncovered",
    edges: "edges",
    antennas: "antennas",
    labels: "coverage_labels",
    visibility: "visibility"
};

const POLYGON_TYPES = new Set(["Polygon", "MultiPolygon"]);
const LINE_TYPES = new Set(["LineString", "MultiLineString"]);
const POINT_TYPES = new Set(["Point", "MultiPoint"]);

const ALLOWED_GEOMETRY_TYPES = {
    source: POLYGON_TYPES,
    covered: POLYGON_TYPES,
    uncovered: POLYGON_TYPES,

    /*
     * The server delivers visibility as fill-only polygons plus their
     * outlines as separate lines, so that cutting a huge polygon into
     * chunks never shows seams.
     */
    visibility: new Set([...POLYGON_TYPES, ...LINE_TYPES]),

    edges: LINE_TYPES,
    antennas: POINT_TYPES,
    labels: POINT_TYPES
};


function acceptsLayerGeometry(layerName, feature) {
    const geometryType = feature?.geometry?.type;

    return Boolean(
        geometryType &&
        ALLOWED_GEOMETRY_TYPES[layerName]?.has(geometryType)
    );
}

/*
 * Visibility fills are drawn opaquely in their own pane, whose opacity
 * provides the transparency. Overlapping fill pieces therefore never
 * darken each other.
 */
const VISIBILITY_FILL_OPACITY = 0.24;

const VIEWPORT_PADDING = 0.25;
const VIEWPORT_REFRESH_DELAY = 180;

/*
 * Chunks stay attached while they overlap the view padded by this
 * ratio. Small pans then never detach and re-attach chunks, while
 * distant chunks are detached to keep canvas redraws fast.
 */
const RETAIN_PADDING = 1.0;

/* Background preloading starts once the view has been idle this long. */
const PRELOAD_DELAY = 400;

/* Comparison chunks are loaded after primary chunks of the same view. */
const COMPARISON_ORDER_OFFSET = 100000;

/* Retries after chunk requests fail because the cache was rebuilt. */
const MAX_STALE_CHUNK_RETRIES = 3;

/*
 * Solution geometries should normally lie within the source extent.
 * A margin avoids clipping antennas or visibility geometry close to
 * the boundary while preventing world-sized low-zoom requests.
 */
const DATA_EXTENT_PADDING_RATIO = 0.10;
const DATA_EXTENT_MIN_PADDING = 0.001;


const state = {
    config: null,
    tileLayer: null,

    sourceLayer: null,
    sourceId: null,
    sourceIndex: createIndexRequestState(),
    sourcePreloadIndex: createIndexRequestState(),

    sourceExtent: null,
    sourceExtentId: null,
    sourceExtentController: null,
    sourceExtentRequestId: 0,
    sourceExtentPendingKey: null,
    sourceExtentPendingPromise: null,
    sourceFittedId: null,

    statistics: null,
    statisticsController: null,
    statisticsRequestId: 0,

    viewportRefreshTimer: null,
    preloadTimer: null,
    loadingStateTimer: null,

    overlayOpen: false,
    overlayInitialised: false,
    visibilityWanted: false,

    mainRequestId: 0,

    pendingRequests: 0,
    readyStatusPending: false,
    staleChunkRetries: 0,

    comparisonLegendElement: null,
    legendElement: null,
    statusCopiedTimer: null
};


const map = L.map("map", {
    center: [50.7374, 7.0982],
    zoom: 14,
    zoomControl: true,
    preferCanvas: true,

    /* The map size is tracked by the observer below instead. */
    trackResize: false
});


/*
 * Follow every size change of the map container: window resizes, but
 * also height changes of the top bar or the comparison panel. The map
 * adapts at most once per frame and redraws its data only after resizing
 * has stopped (debounceMoveend), which keeps resizing smooth.
 */
let mapResizeFrame = null;

new ResizeObserver(() => {
    if (mapResizeFrame !== null) {
        return;
    }

    mapResizeFrame = window.requestAnimationFrame(() => {
        mapResizeFrame = null;
        map.invalidateSize({debounceMoveend: true});
    });
}).observe(map.getContainer());


const paneDefinitions = [
    ["primaryVisibilityFillPane", 390],
    ["primaryVisibilityOutlinePane", 391],
    ["comparisonVisibilityFillPane", 395],
    ["comparisonVisibilityOutlinePane", 396],

    ["sourcePane", 410],

    ["primaryCoveragePane", 420],
    ["comparisonCoveragePane", 425],

    ["primaryEdgePane", 450],
    ["comparisonEdgePane", 455],

    ["primaryAntennaPane", 460],
    ["comparisonAntennaPane", 465],
    ["sharedAntennaPane", 468],

    ["primaryLabelPane", 470],
    ["comparisonLabelPane", 475]
];

for (const [paneName, zIndex] of paneDefinitions) {
    const pane = map.createPane(paneName);

    pane.style.zIndex = String(zIndex);
    pane.style.transition = "opacity 90ms linear";
}


const chunkStore = new ChunkStore(CHUNK_STORE_MAX_BYTES);
const chunkLoader = new ChunkLoader(chunkStore);
const chunkBuildQueue = new ChunkBuildQueue();

chunkLoader.onChange(requestLoadingStateUpdate);


function createChunkedLayer(owner, createLayer, onAttach = null) {
    return new ChunkedLayer({
        owner,
        loader: chunkLoader,
        store: chunkStore,
        buildQueue: chunkBuildQueue,
        createLayer,
        onAttach,
        onChange: requestLoadingStateUpdate,
        onError: handleChunkError
    });
}


function createSourceLayer() {
    return L.geoJSON(null, {
        pane: "sourcePane",
        interactive: false,

        filter(feature) {
            return acceptsLayerGeometry("source", feature);
        },

        style: {
            color: "#173f68",
            weight: 1.5,
            opacity: 0.95,
            fillColor: "#7da8ce",
            fillOpacity: 0.10
        }
    });
}

state.sourceLayer = createChunkedLayer(
    "source",
    createSourceLayer
);
state.sourceLayer.group.addTo(map);


function createSolutionBundle(role, theme, panes) {
    const bundle = {
        role,
        theme,
        panes,

        ready: false,
        alpha: role === "primary" ? 1 : 0,

        selection: null,
        key: null,

        /* Incremented whenever the bundle's data is discarded. */
        generation: 0,

        layers: {},

        /* Runs found for the selection (from the chunk index). */
        runs: null,

        viewportIndex: createIndexRequestState(),
        preloadIndex: createIndexRequestState()
    };

    const onAttach = {
        edges: refreshEdgeStyle,
        antennas: scheduleSharedAntennaUpdate
    };

    for (const layerName of ALL_SOLUTION_LAYER_NAMES) {
        bundle.layers[layerName] = createChunkedLayer(
            `${role}:${layerName}`,
            () => createBundleLayer(bundle, layerName),
            onAttach[layerName] ?? null
        );
    }

    return bundle;
}


function createBundleLayer(bundle, layerName) {
    const {role, theme, panes} = bundle;

    const filter = feature => (
        acceptsLayerGeometry(layerName, feature)
    );

    if (layerName === "visibility") {
        /*
         * Fill polygons and outline lines go to separate panes. The
         * fills are opaque; their pane provides the transparency (see
         * VISIBILITY_FILL_OPACITY), so overlapping pieces show no seams.
         */
        const fill = L.geoJSON(null, {
            pane: panes.visibilityFill,
            interactive: false,

            filter(feature) {
                return POLYGON_TYPES.has(feature?.geometry?.type);
            },

            style: {
                stroke: false,
                fillColor: theme.visibilityFill,
                fillOpacity: 1
            }
        });

        const outline = L.geoJSON(null, {
            pane: panes.visibilityOutline,
            interactive: false,

            filter(feature) {
                return LINE_TYPES.has(feature?.geometry?.type);
            },

            style: {
                color: theme.visibility,
                weight: 1.2,
                opacity: 0.55,
                fill: false,
                lineCap: "butt",
                lineJoin: "round"
            }
        });

        const group = L.layerGroup([fill, outline]);

        group.addData = data => {
            fill.addData(data);
            outline.addData(data);

            return group;
        };

        return group;
    }

    if (layerName === "covered") {
        return L.geoJSON(null, {
            pane: panes.coverage,
            interactive: false,
            filter,

            style: {
                color: theme.covered,
                weight: 1.4,
                opacity: 0.9,
                fillColor: theme.coveredFill,
                fillOpacity: 0.12
            }
        });
    }

    if (layerName === "uncovered") {
        return L.geoJSON(null, {
            pane: panes.coverage,
            interactive: false,
            filter,

            style: {
                color: theme.uncovered,
                weight: 1.4,
                opacity: 0.9,
                fillColor: theme.uncoveredFill,
                fillOpacity: 0.12
            }
        });
    }

    if (layerName === "edges") {
        const layer = L.geoJSON(null, {
            pane: panes.edge,
            interactive: false,
            filter,

            style(feature) {
                const isOnUncoveredBuilding = (
                    feature?.properties?.status === "uncovered"
                );

                /*
                 * Edges on covered buildings are drawn bolder than edges
                 * on uncovered buildings, which also differ by colour.
                 */
                const roleWeight = role === "primary" ? 3.2 : 3.6;
                const baseWeight = roleWeight * (
                    isOnUncoveredBuilding ? 0.8 : 1.2
                );

                return {
                    color: isOnUncoveredBuilding
                        ? theme.uncovered
                        : theme.edge,

                    weight: baseWeight * edgeWeightScale(),

                    opacity: 0.98,
                    lineCap: "round",
                    lineJoin: "round",
                    dashArray: null
                };
            }
        });

        /* See refreshEdgeStyle. */
        layer.edgeWeightScale = edgeWeightScale();

        return layer;
    }

    if (layerName === "antennas") {
        return L.geoJSON(null, {
            pane: panes.antenna,
            filter,

            pointToLayer(feature, latlng) {
                const icon = L.divIcon({
                    className: "antenna-icon-shell",
                    html: `
                        <span
                            class="
                                antenna-marker
                                antenna-marker-${role}
                            "
                            aria-hidden="true"
                        >
                            <span
                                class="antenna-marker-centre"
                            ></span>
                        </span>
                    `,
                    iconSize: [30, 30],
                    iconAnchor: [15, 15]
                });

                return L.marker(latlng, {
                    pane: panes.antenna,
                    icon,
                    keyboard: true,
                    title: `${theme.name} antenna`
                });
            },

            onEachFeature(feature, layer) {
                layer.bindTooltip(
                    `${theme.name} antenna`,
                    {
                        direction: "top",
                        offset: [0, -13]
                    }
                );
            }
        });
    }

    if (layerName === "labels") {
        return L.geoJSON(null, {
            pane: panes.label,
            filter,

            pointToLayer(feature, latlng) {
                const properties = feature.properties || {};

                const coverageStatus =
                    properties.status === "covered"
                        ? "covered"
                        : "uncovered";

                const coverage = formatCoverage(
                    properties.coverage
                );

                const safeCoverage = escapeHtml(coverage);

                const icon = L.divIcon({
                    className: "coverage-marker-shell",
                    html: `
                        <span
                            class="
                                coverage-marker
                                coverage-marker-${role}
                                coverage-marker-${role}-${coverageStatus}
                            "
                        >
                            ${safeCoverage}
                        </span>
                    `,
                    iconSize: null,
                    iconAnchor: [0, 0]
                });

                return L.marker(latlng, {
                    pane: panes.label,
                    icon,
                    keyboard: true,
                    title: (
                        `${theme.name} coverage: ${coverage}`
                    )
                });
            },

            onEachFeature(feature, layer) {
                const properties = feature.properties || {};

                const coverageStatus =
                    properties.status === "covered"
                        ? "Covered"
                        : "Uncovered";

                const coverage = escapeHtml(
                    formatCoverage(properties.coverage)
                );

                layer.bindTooltip(
                    `
                        <strong>
                            ${theme.name}: ${coverageStatus}
                        </strong>
                        <br>
                        Coverage: ${coverage}
                    `,
                    {
                        direction: "top",
                        offset: [
                            0,
                            role === "primary" ? -20 : 6
                        ]
                    }
                );
            }
        });
    }

    throw new Error(`Unknown frontend layer '${layerName}'.`);
}


const primaryBundle = createSolutionBundle(
    "primary",
    PRIMARY_THEME,
    {
        visibilityFill: "primaryVisibilityFillPane",
        visibilityOutline: "primaryVisibilityOutlinePane",
        coverage: "primaryCoveragePane",
        edge: "primaryEdgePane",
        antenna: "primaryAntennaPane",
        label: "primaryLabelPane"
    }
);

const comparisonBundle = createSolutionBundle(
    "comparison",
    COMPARISON_THEME,
    {
        visibilityFill: "comparisonVisibilityFillPane",
        visibilityOutline: "comparisonVisibilityOutlinePane",
        coverage: "comparisonCoveragePane",
        edge: "comparisonEdgePane",
        antenna: "comparisonAntennaPane",
        label: "comparisonLabelPane"
    }
);


/*
 * Shared antennas
 *
 * In comparison mode, an antenna that both solutions place at exactly the
 * same position would be covered by the comparison marker. Such antennas
 * are drawn as one split marker instead (see .antenna-marker-shared in
 * styles.css): the two individual markers are hidden, and a marker in its
 * own pane above both solutions shows the primary colour on the left and
 * the comparison colour on the right, divided at the fade slider's
 * position.
 */
const sharedAntennaLayer = L.layerGroup().addTo(map);

/* Individual markers currently hidden behind a shared marker. */
let hiddenSharedAntennaMarkers = [];

let sharedAntennaFrame = null;


function scheduleSharedAntennaUpdate() {
    if (sharedAntennaFrame !== null) {
        return;
    }

    sharedAntennaFrame = window.requestAnimationFrame(() => {
        sharedAntennaFrame = null;
        updateSharedAntennas();
    });
}


/*
 * A solution's antenna markers, grouped by position. The server rounds
 * all coordinates to 7 decimal places, so equal positions compare equal.
 */
function antennaMarkersByPosition(bundle) {
    const result = new Map();

    bundle.layers.antennas.forEachAttachedLayer(chunk => {
        chunk.eachLayer(marker => {
            if (typeof marker.getLatLng !== "function") {
                return;
            }

            const position = marker.getLatLng();
            const key = (
                `${position.lat.toFixed(7)},${position.lng.toFixed(7)}`
            );

            if (!result.has(key)) {
                result.set(key, []);
            }

            result.get(key).push(marker);
        });
    });

    return result;
}


function createSharedAntennaMarker(position) {
    const description = "Primary and comparison antenna";

    const icon = L.divIcon({
        className: "antenna-icon-shell",
        html: `
            <span
                class="antenna-marker antenna-marker-shared"
                aria-hidden="true"
            >
                <span class="antenna-marker-centre"></span>
            </span>
        `,
        iconSize: [30, 30],
        iconAnchor: [15, 15]
    });

    return L.marker(position, {
        pane: "sharedAntennaPane",
        icon,
        keyboard: true,
        title: description
    }).bindTooltip(description, {
        direction: "top",
        offset: [0, -13]
    });
}


function updateSharedAntennas() {
    for (const marker of hiddenSharedAntennaMarkers) {
        marker.setOpacity(1);
    }

    hiddenSharedAntennaMarkers = [];
    sharedAntennaLayer.clearLayers();

    if (
        !state.overlayOpen ||
        !primaryBundle.ready ||
        !comparisonBundle.ready
    ) {
        return;
    }

    const comparisonMarkers = antennaMarkersByPosition(
        comparisonBundle
    );

    for (const [key, primaryMarkers] of antennaMarkersByPosition(
        primaryBundle
    )) {
        const matchingMarkers = comparisonMarkers.get(key);

        if (!matchingMarkers) {
            continue;
        }

        for (const marker of [...primaryMarkers, ...matchingMarkers]) {
            marker.setOpacity(0);
            hiddenSharedAntennaMarkers.push(marker);
        }

        sharedAntennaLayer.addLayer(
            createSharedAntennaMarker(primaryMarkers[0].getLatLng())
        );
    }
}


L.control.scale({
    imperial: false,
    position: "bottomleft"
}).addTo(map);

addLegend();
setBundleAlpha(primaryBundle, 0);
setBundleAlpha(comparisonBundle, 0);


function addLegend() {
    const LegendControl = L.Control.extend({
        options: {
            position: "bottomright"
        },

        onAdd() {
            const container = L.DomUtil.create(
                "div",
                "map-legend"
            );

            container.innerHTML = `
                <div class="legend-title">Map layers</div>

                <div class="legend-row legend-source-row">
                    <span
                        class="legend-symbol legend-building"
                    ></span>
                    Source buildings
                </div>

                <div class="legend-section-title">
                    Primary solution
                </div>

                <div class="legend-row">
                    <span
                        class="
                            legend-symbol
                            legend-primary-antenna
                        "
                    ></span>
                    Antennas
                </div>

                <div class="legend-row">
                    <span
                        class="
                            legend-symbol
                            legend-primary-covered
                        "
                    ></span>
                    Covered
                </div>

                <div class="legend-row">
                    <span
                        class="
                            legend-symbol
                            legend-primary-uncovered
                        "
                    ></span>
                    Uncovered
                </div>

                <div class="legend-row">
                    <span
                        class="
                            legend-symbol
                            legend-primary-edge
                        "
                    ></span>
                    Visible edges
                </div>

                <div class="comparison-legend-section">
                    <div class="legend-section-title">
                        Comparison solution
                    </div>

                    <div class="legend-row">
                        <span
                            class="
                                legend-symbol
                                legend-comparison-antenna
                            "
                        ></span>
                        Antennas
                    </div>

                    <div class="legend-row">
                        <span
                            class="
                                legend-symbol
                                legend-shared-antenna
                            "
                        ></span>
                        Shared antennas
                    </div>

                    <div class="legend-row">
                        <span
                            class="
                                legend-symbol
                                legend-comparison-covered
                            "
                        ></span>
                        Covered
                    </div>

                    <div class="legend-row">
                        <span
                            class="
                                legend-symbol
                                legend-comparison-uncovered
                            "
                        ></span>
                        Uncovered
                    </div>

                    <div class="legend-row">
                        <span
                            class="
                                legend-symbol
                                legend-comparison-edge
                            "
                        ></span>
                        Visible edges
                    </div>
                </div>
            `;

            state.comparisonLegendElement =
                container.querySelector(
                    ".comparison-legend-section"
                );

            state.legendElement = container;

            L.DomEvent.disableClickPropagation(container);
            L.DomEvent.disableScrollPropagation(container);

            return container;
        }
    });

    map.addControl(new LegendControl());
}


function addOptions(selectElement, values) {
    for (const value of values) {
        const option = document.createElement("option");

        if (typeof value === "object") {
            option.value = value.id;
            option.textContent = value.label;
        } else {
            option.value = String(value);
            option.textContent = String(value);
        }

        selectElement.appendChild(option);
    }
}


function setStatus(message, stateName = "ready") {
    statusTextElement.textContent = message;
    statusTextElement.scrollTop = 0;
    statusElement.classList.remove("is-copied");
    statusElement.title = `${message}\n\nClick to copy this message.`;

    statusElement.dataset.state = stateName;
    updateStatusOverflow();
}


/* Fade the last visible line while more status text follows below. */
function updateStatusOverflow() {
    const hiddenBelow = (
        statusTextElement.scrollHeight -
        statusTextElement.clientHeight -
        statusTextElement.scrollTop
    );

    statusTextElement.classList.toggle("has-more", hiddenBelow > 1);
}


function beginLoading() {
    state.pendingRequests += 1;
    requestLoadingStateUpdate();
}


function finishLoading() {
    state.pendingRequests = Math.max(
        0,
        state.pendingRequests - 1
    );

    requestLoadingStateUpdate();
}


/*
 * The loading state is evaluated asynchronously. A chunk index request
 * finishes just before its chunks are queued; evaluating synchronously
 * would report an idle map in between.
 */
function requestLoadingStateUpdate() {
    if (state.loadingStateTimer !== null) {
        return;
    }

    state.loadingStateTimer = window.setTimeout(() => {
        state.loadingStateTimer = null;
        updateLoadingState();
    }, 0);
}


function allChunkedLayers() {
    const layers = [state.sourceLayer];

    for (const bundle of [primaryBundle, comparisonBundle]) {
        for (const layerName of ALL_SOLUTION_LAYER_NAMES) {
            layers.push(bundle.layers[layerName]);
        }
    }

    return layers;
}


function isViewportLoading() {
    return (
        state.pendingRequests > 0 ||
        allChunkedLayers().some(layer => layer.isLoading())
    );
}


function updateLoadingState() {
    const loading = isViewportLoading();

    mapShell.classList.toggle("is-loading", loading);

    if (loading) {
        return;
    }

    if (state.readyStatusPending) {
        state.readyStatusPending = false;

        const warning = multipleRunsWarning();

        if (warning) {
            setStatus(`${readyStatusMessage()} ${warning}`, "warning");
        } else {
            setStatus(readyStatusMessage(), "ready");
        }
    }

    schedulePreload();
}


/*
 * Warning for shown solutions with several runs of the same source,
 * algorithm, k, and tau. The server shows the best run (most covered
 * buildings); null if no shown solution has several runs.
 */
function multipleRunsWarning() {
    const parts = [];

    for (const bundle of [primaryBundle, comparisonBundle]) {
        const runs = bundle.runs;

        if (
            !bundleIsShown(bundle) ||
            !bundle.selection ||
            !runs ||
            runs.count < 2
        ) {
            continue;
        }

        const name = bundle.role === "primary"
            ? "Primary"
            : "Comparison";

        const best = Number.isFinite(runs.selected_value)
            ? `the best one, ${runs.selected_value.toLocaleString()} covered`
            : "the newest one";

        parts.push(
            `${name} (k ${bundle.selection.k}, τ ${bundle.selection.tau}): ` +
            `${runs.count} runs found, showing ${best}.`
        );
    }

    return parts.length > 0
        ? `Warning: ${parts.join(" ")}`
        : null;
}


function readyStatusMessage() {
    if (!primaryBundle.selection) {
        return "Source loaded — no algorithm selected.";
    }

    if (state.overlayOpen && comparisonBundle.selection) {
        return comparisonStatusMessage();
    }

    return primaryStatusMessage();
}


function reportLoadingError(error, prefix = "") {
    if (error?.name === "AbortError") {
        return;
    }

    console.error(error);

    state.readyStatusPending = false;

    setStatus(
        prefix +
            (error?.message || "Map data could not be loaded."),
        "error"
    );
}


function handleChunkError(error) {
    if (error?.name === "AbortError") {
        return;
    }

    if (
        error?.status === 404 &&
        state.staleChunkRetries < MAX_STALE_CHUNK_RETRIES
    ) {
        /*
         * The server rebuilt a cache after its chunk index was loaded.
         * A fresh index carries the new version; the layers then replace
         * their old chunks automatically.
         */
        state.staleChunkRetries += 1;
        invalidateChunkIndexes();
        scheduleViewportRefresh();
        return;
    }

    reportLoadingError(
        error,
        "Some map data could not be loaded: "
    );
}


function clamp(value, minimum, maximum) {
    return Math.min(
        maximum,
        Math.max(minimum, value)
    );
}


function formatCoverage(value) {
    if (
        value === null ||
        value === undefined ||
        value === ""
    ) {
        return "n/a";
    }

    const numericValue = Number(value);
    const mapConfig = state.config?.map || {};

    const multiplier = Number(
        mapConfig.coverage_label_multiplier ?? 1
    );

    const decimalPlaces = clamp(
        Number(
            mapConfig.coverage_label_decimals ?? 2
        ),
        0,
        8
    );

    const suffix = String(
        mapConfig.coverage_label_suffix ?? ""
    );

    if (Number.isFinite(numericValue)) {
        const displayValue = numericValue * multiplier;

        return (
            displayValue.toLocaleString(undefined, {
                minimumFractionDigits: 0,
                maximumFractionDigits: decimalPlaces
            }) + suffix
        );
    }

    return String(value) + suffix;
}


function escapeHtml(value) {
    return String(value)
        .replaceAll("&", "&amp;")
        .replaceAll("<", "&lt;")
        .replaceAll(">", "&gt;")
        .replaceAll('"', "&quot;")
        .replaceAll("'", "&#039;");
}


async function fetchJson(url, signal = undefined) {
    let response;

    try {
        response = await fetch(url, {
            signal,
            headers: {
                "Accept": (
                    "application/json, application/geo+json"
                )
            }
        });
    } catch (error) {
        if (
            signal?.aborted ||
            error?.name === "AbortError"
        ) {
            throw createAbortError();
        }

        throw new Error(
            `Could not connect to the server for '${url}': ` +
            `${error?.message || "network error"}`
        );
    }

    let payload;

    try {
        payload = await response.json();
    } catch (error) {
        /*
         * response.json() also throws AbortError when the request is
         * cancelled while its response body is still being read.
         * Do not turn that normal cancellation into a data error.
         */
        if (
            signal?.aborted ||
            error?.name === "AbortError"
        ) {
            throw createAbortError();
        }

        const contentType = (
            response.headers.get("content-type") ||
            "unknown content type"
        );

        const contentLength = (
            response.headers.get("content-length") ||
            "unknown length"
        );

        if (!response.ok) {
            throw new Error(
                `Request '${url}' failed with HTTP ` +
                `${response.status}, and the error response was ` +
                `not valid JSON (${contentType}, ` +
                `${contentLength} bytes).`
            );
        }

        throw new Error(
            `The server returned invalid or empty JSON for ` +
            `'${url}' (HTTP ${response.status}, ` +
            `${contentType}, ${contentLength} bytes). ` +
            `Parser error: ${error?.message || "unknown error"}`
        );
    }

    if (!response.ok) {
        let message =
            `Request '${url}' failed with HTTP ${response.status}.`;

        if (payload?.detail !== undefined) {
            message = typeof payload.detail === "string"
                ? payload.detail
                : JSON.stringify(payload.detail);
        }

        throw new Error(message);
    }

    if (payload === null || payload === undefined) {
        throw new Error(
            `The server returned an empty JSON value for '${url}'.`
        );
    }

    return payload;
}


function makeSelection(algorithm, k, tau) {
    return {
        source: sourceSelect.value,
        algorithm,
        k,
        tau
    };
}


function selectionKey(selection) {
    if (!selection) {
        return "";
    }

    return [
        selection.source,
        selection.algorithm,
        selection.k,
        selection.tau
    ].map(String).join("|");
}


function bboxParameter(bbox) {
    return bbox
        .map(value => value.toFixed(7))
        .join(",");
}


function centreParameter() {
    const centre = map.getCenter();

    return [
        normaliseLongitude(centre.lng).toFixed(6),
        clamp(centre.lat, -90, 90).toFixed(6)
    ].join(",");
}


function sourceChunkIndexUrl(sourceId, bbox) {
    const query = new URLSearchParams();

    query.set("bbox", bboxParameter(bbox));
    query.set("center", centreParameter());

    return (
        `/api/source/${encodeURIComponent(sourceId)}/chunks` +
        `?${query.toString()}`
    );
}


function solutionChunkIndexUrl(selection, layerNames, bbox) {
    const query = new URLSearchParams(selection);

    query.set(
        "layers",
        layerNames
            .map(layerName => API_LAYER_NAMES[layerName])
            .join(",")
    );
    query.set("bbox", bboxParameter(bbox));
    query.set("center", centreParameter());

    return `/api/solution/chunks?${query.toString()}`;
}


function createIndexRequestState() {
    return {
        controller: null,
        pendingUrl: null,
        pendingPromise: null,
        lastUrl: null,
        lastPayload: null
    };
}


function abortChunkIndex(indexState) {
    if (indexState.controller) {
        indexState.controller.abort();
    }

    indexState.controller = null;
    indexState.pendingUrl = null;
    indexState.pendingPromise = null;
}


function resetChunkIndex(indexState) {
    abortChunkIndex(indexState);

    indexState.lastUrl = null;
    indexState.lastPayload = null;
}


function invalidateChunkIndexes() {
    resetChunkIndex(state.sourceIndex);
    resetChunkIndex(state.sourcePreloadIndex);

    for (const bundle of [primaryBundle, comparisonBundle]) {
        resetChunkIndex(bundle.viewportIndex);
        resetChunkIndex(bundle.preloadIndex);
    }
}


/*
 * Request the list of chunks for an index URL. A repeated request for
 * the latest URL is answered from memory; a request for a new URL
 * aborts the previous one of the same index state. Background requests
 * do not show the loading indicator.
 */
function loadChunkIndex(indexState, url, background = false) {
    if (
        indexState.lastUrl === url &&
        indexState.lastPayload
    ) {
        return Promise.resolve(indexState.lastPayload);
    }

    if (
        indexState.pendingUrl === url &&
        indexState.pendingPromise
    ) {
        return indexState.pendingPromise;
    }

    abortChunkIndex(indexState);

    const controller = new AbortController();

    indexState.controller = controller;
    indexState.pendingUrl = url;

    if (!background) {
        beginLoading();
    }

    const operation = (async () => {
        try {
            const payload = await fetchJson(url, controller.signal);

            if (controller.signal.aborted) {
                throw createAbortError();
            }

            if (
                !Array.isArray(payload.chunks) ||
                typeof payload.chunk_url_prefix !== "string" ||
                typeof payload.version !== "string"
            ) {
                throw new Error(
                    "The server returned an invalid chunk index."
                );
            }

            indexState.lastUrl = url;
            indexState.lastPayload = payload;

            checkCatalogVersion(payload.catalog_version);

            return payload;
        } finally {
            if (indexState.controller === controller) {
                indexState.controller = null;
                indexState.pendingUrl = null;
                indexState.pendingPromise = null;
            }

            if (!background) {
                finishLoading();
            }
        }
    })();

    indexState.pendingPromise = operation;

    return operation;
}


function chunkUrl(payload, chunk) {
    return (
        `${payload.chunk_url_prefix}/` +
        `${encodeURIComponent(chunk.layer)}/` +
        `${encodeURIComponent(chunk.id)}`
    );
}


/*
 * Group an index's chunks by server layer name. Each descriptor keeps
 * its position in the server's centre-out order.
 */
function chunkDescriptorsByLayer(payload) {
    const result = new Map();

    payload.chunks.forEach((chunk, position) => {
        if (!result.has(chunk.layer)) {
            result.set(chunk.layer, []);
        }

        result.get(chunk.layer).push({
            url: chunkUrl(payload, chunk),
            bbox: chunk.bbox,
            bytes: Number(chunk.bytes) || 0,
            count: Number(chunk.count) || 0,
            order: position
        });
    });

    return result;
}


function sourceExtentRequestUrl(sourceId) {
    return (
        `/api/source/${encodeURIComponent(sourceId)}` +
        "/extent"
    );
}


function parseBbox(rawBbox) {
    if (
        !Array.isArray(rawBbox) ||
        rawBbox.length !== 4
    ) {
        throw new Error(
            "The server returned an invalid source extent."
        );
    }

    const bbox = rawBbox.map(Number);

    if (!bbox.every(Number.isFinite)) {
        throw new Error(
            "The source extent contains non-finite coordinates."
        );
    }

    if (
        bbox[0] >= bbox[2] ||
        bbox[1] >= bbox[3]
    ) {
        throw new Error(
            "The server returned a degenerate source extent."
        );
    }

    return bbox;
}


async function ensureSourceExtent(sourceId) {
    if (
        state.sourceExtentId === sourceId &&
        state.sourceExtent
    ) {
        return state.sourceExtent;
    }

    if (
        state.sourceExtentPendingKey === sourceId &&
        state.sourceExtentPendingPromise
    ) {
        return state.sourceExtentPendingPromise;
    }

    if (state.sourceExtentController) {
        state.sourceExtentController.abort();
    }

    const controller = new AbortController();
    const requestId = ++state.sourceExtentRequestId;

    state.sourceExtentController = controller;
    state.sourceExtentPendingKey = sourceId;

    const operation = (async () => {
        beginLoading();

        try {
            const payload = await fetchJson(
                sourceExtentRequestUrl(sourceId),
                controller.signal
            );

            if (
                controller.signal.aborted ||
                requestId !== state.sourceExtentRequestId ||
                sourceSelect.value !== sourceId
            ) {
                throw createAbortError();
            }

            const bbox = parseBbox(payload.bbox);

            state.sourceExtent = bbox;
            state.sourceExtentId = sourceId;

            return bbox;
        } finally {
            if (
                state.sourceExtentController === controller
            ) {
                state.sourceExtentController = null;
                state.sourceExtentPendingKey = null;
                state.sourceExtentPendingPromise = null;
            }

            finishLoading();
        }
    })();

    state.sourceExtentPendingPromise = operation;

    return operation;
}


async function ensureSourceExtentAndFit(sourceId) {
    const bbox = await ensureSourceExtent(sourceId);

    if (sourceSelect.value !== sourceId) {
        throw createAbortError();
    }

    if (state.sourceFittedId === sourceId) {
        return bbox;
    }

    const bounds = L.latLngBounds(
        [bbox[1], bbox[0]],
        [bbox[3], bbox[2]]
    );

    if (!bounds.isValid()) {
        throw new Error(
            "The selected source has an invalid map extent."
        );
    }

    const maximumZoom = Number(
        state.config?.map?.max_zoom ?? 20
    );

    map.fitBounds(bounds, {
        padding: [24, 24],
        maxZoom: Math.min(18, maximumZoom),
        animate: false
    });

    const configuredOffset = Number(
        state.config?.map?.initial_source_zoom_offset ?? 1
    );

    const zoomOffset = clamp(
        Number.isFinite(configuredOffset)
            ? Math.trunc(configuredOffset)
            : 1,
        0,
        4
    );

    if (zoomOffset > 0) {
        map.setZoom(
            Math.min(
                maximumZoom,
                map.getZoom() + zoomOffset
            ),
            {
                animate: false
            }
        );
    }

    state.sourceFittedId = sourceId;

    return bbox;
}


function normaliseLongitude(value) {
    return (
        ((Number(value) + 180) % 360 + 360) % 360
    ) - 180;
}


function leafletBoundsToBbox(bounds) {
    const rawWest = Number(bounds.getWest());
    const rawEast = Number(bounds.getEast());
    const rawSouth = Number(bounds.getSouth());
    const rawNorth = Number(bounds.getNorth());

    if (
        ![
            rawWest,
            rawEast,
            rawSouth,
            rawNorth
        ].every(Number.isFinite)
    ) {
        return null;
    }

    const south = clamp(rawSouth, -90, 90);
    const north = clamp(rawNorth, -90, 90);

    if (south >= north) {
        return null;
    }

    const longitudeWidth = rawEast - rawWest;

    if (
        !Number.isFinite(longitudeWidth) ||
        longitudeWidth <= 0
    ) {
        return null;
    }

    /*
     * A view spanning the world, or crossing the antimeridian,
     * cannot be represented by one ordinary min/max bbox. Use the
     * complete longitude range in that case.
     */
    if (longitudeWidth >= 360) {
        return [-180, south, 180, north];
    }

    const rawCentre = (rawWest + rawEast) / 2;
    const centre = normaliseLongitude(rawCentre);

    const west = centre - longitudeWidth / 2;
    const east = centre + longitudeWidth / 2;

    if (west < -180 || east > 180) {
        return [-180, south, 180, north];
    }

    return [west, south, east, north];
}


function padBbox(bbox, ratio, minimumPadding) {
    const width = bbox[2] - bbox[0];
    const height = bbox[3] - bbox[1];

    const longitudePadding = Math.max(
        width * ratio,
        minimumPadding
    );

    const latitudePadding = Math.max(
        height * ratio,
        minimumPadding
    );

    return [
        clamp(
            bbox[0] - longitudePadding,
            -180,
            180
        ),
        clamp(
            bbox[1] - latitudePadding,
            -90,
            90
        ),
        clamp(
            bbox[2] + longitudePadding,
            -180,
            180
        ),
        clamp(
            bbox[3] + latitudePadding,
            -90,
            90
        )
    ];
}


function intersectBboxes(first, second) {
    if (!first || !second) {
        return null;
    }

    const intersection = [
        Math.max(first[0], second[0]),
        Math.max(first[1], second[1]),
        Math.min(first[2], second[2]),
        Math.min(first[3], second[3])
    ];

    if (
        intersection[0] >= intersection[2] ||
        intersection[1] >= intersection[3]
    ) {
        return null;
    }

    return intersection;
}


function currentDataExtent() {
    if (
        !state.sourceExtent ||
        state.sourceExtentId !== sourceSelect.value
    ) {
        return null;
    }

    return padBbox(
        state.sourceExtent,
        DATA_EXTENT_PADDING_RATIO,
        DATA_EXTENT_MIN_PADDING
    );
}


function currentViewportRequest() {
    const visibleWorldBbox = leafletBoundsToBbox(
        map.getBounds()
    );

    const paddedWorldBbox = leafletBoundsToBbox(
        map.getBounds().pad(VIEWPORT_PADDING)
    );

    if (!visibleWorldBbox || !paddedWorldBbox) {
        return {
            bbox: null,
            visibleBbox: null,
            key: "invalid"
        };
    }

    const dataExtent = currentDataExtent();

    const visibleBbox = dataExtent
        ? intersectBboxes(
            visibleWorldBbox,
            dataExtent
        )
        : visibleWorldBbox;

    const bbox = dataExtent
        ? intersectBboxes(
            paddedWorldBbox,
            dataExtent
        )
        : paddedWorldBbox;

    return {
        bbox,
        visibleBbox,
        key: bbox
            ? bbox
                .map(value => value.toFixed(6))
                .join(",")
            : "outside-source"
    };
}

function preloadOwner(name) {
    return `preload:${name}`;
}


function resetBundleData(bundle) {
    bundle.generation += 1;
    bundle.runs = null;

    resetChunkIndex(bundle.viewportIndex);
    resetChunkIndex(bundle.preloadIndex);
    chunkLoader.cancelOwner(preloadOwner(bundle.role));

    for (const layerName of ALL_SOLUTION_LAYER_NAMES) {
        bundle.layers[layerName].clear();
    }

    /* The cleared antennas no longer match the other solution's. */
    scheduleSharedAntennaUpdate();
}


function prepareBundleSelection(bundle, selection) {
    const newKey = selectionKey(selection);

    if (bundle.ready && bundle.key === newKey) {
        return false;
    }

    /*
     * The bundle becomes active immediately, but all its layers start
     * empty until the new solution's chunks arrive.
     */
    resetBundleData(bundle);

    bundle.selection = {
        ...selection
    };
    bundle.key = newKey;
    bundle.ready = true;

    updateBlend();

    return true;
}


function deactivateBundle(bundle) {
    resetBundleData(bundle);

    bundle.selection = null;
    bundle.key = null;
    bundle.ready = false;

    updateBlend();
}


function setPaneOpacity(paneName, opacity) {
    const pane = map.getPane(paneName);

    if (!pane) {
        return;
    }

    pane.style.opacity = String(opacity);
    pane.style.visibility = opacity <= 0.001
        ? "hidden"
        : "visible";
}


function setBundleAlpha(bundle, alpha) {
    const safeAlpha = clamp(Number(alpha), 0, 1);

    bundle.alpha = safeAlpha;

    /*
     * In comparison mode, antennas and labels reach full opacity
     * when their solution reaches a 50% share.
     */
    const pointAlpha = state.overlayOpen
        ? clamp(safeAlpha * 2, 0, 1)
        : safeAlpha;

    setPaneOpacity(
        bundle.panes.visibilityFill,
        safeAlpha * VISIBILITY_FILL_OPACITY
    );

    for (const paneName of [
        bundle.panes.visibilityOutline,
        bundle.panes.coverage,
        bundle.panes.edge
    ]) {
        setPaneOpacity(paneName, safeAlpha);
    }

    for (const paneName of [
        bundle.panes.antenna,
        bundle.panes.label
    ]) {
        setPaneOpacity(paneName, pointAlpha);
    }
}


function setLayerPresence(layer, shouldBePresent) {
    if (shouldBePresent && !map.hasLayer(layer)) {
        layer.addTo(map);
    } else if (!shouldBePresent && map.hasLayer(layer)) {
        map.removeLayer(layer);
    }
}


function bundleIsVisiblyActive(bundle) {
    if (!bundle.ready || bundle.alpha <= 0.001) {
        return false;
    }

    if (bundle.role === "comparison") {
        return state.overlayOpen;
    }

    return true;
}


function bundleIsShown(bundle) {
    return Boolean(
        bundle.ready &&
        (
            bundle.role === "primary" ||
            state.overlayOpen
        )
    );
}


function shouldBundleLayerBePresent(bundle, layerName) {
    if (!state.config || !bundleIsShown(bundle)) {
        return false;
    }

    if (BASE_LAYER_NAMES.includes(layerName)) {
        return true;
    }

    /* Coverage labels belong to the visibility view. */
    /*
     * Labels and visibility areas stay on the map while the fade slider
     * hides their solution: the pane opacity alone fades them (see
     * setBundleAlpha). Removing them would make them pop in again, after
     * a reload, instead of fading in smoothly.
     */
    if (layerName === "labels") {
        const minimumZoom = Number(
            state.config.map.coverage_label_min_zoom ?? 17
        );

        return (
            state.visibilityWanted &&
            map.getZoom() >= minimumZoom
        );
    }

    if (layerName === "visibility") {
        return state.visibilityWanted;
    }

    return false;
}


/*
 * The source buildings are only drawn while no solution is selected. A
 * solution's covered and uncovered buildings together are exactly the
 * source buildings, so drawing the source below them would only cost
 * rendering time.
 */
function sourceLayerShown() {
    return !primaryBundle.ready;
}


function presentBundleLayerNames(bundle) {
    return ALL_SOLUTION_LAYER_NAMES.filter(
        layerName => shouldBundleLayerBePresent(
            bundle,
            layerName
        )
    );
}


function updateZoomDependentStyles() {
    const zoom = map.getZoom();

    const scale = clamp(
        0.60 + (zoom - 13) * 0.10,
        0.58,
        1
    );

    map.getContainer().style.setProperty(
        "--antenna-scale",
        String(scale)
    );

    for (const bundle of [primaryBundle, comparisonBundle]) {
        bundle.layers.edges.forEachAttachedLayer(refreshEdgeStyle);
    }
}


/*
 * Edge widths grow with the zoom level: clearly thinner when zoomed out,
 * slightly bolder when zoomed in all the way. The base widths apply at
 * zoom 17.
 */
function edgeWeightScale() {
    const zoom = map.getZoom();

    const scale = zoom < 17
        ? 1 - (17 - zoom) * 0.18
        : 1 + (zoom - 17) * 0.11;

    return Math.round(
        clamp(scale, 0.3, 1.35) * 100
    ) / 100;
}


/*
 * Restyle an edge chunk drawn for another zoom level. Only chunks on the
 * map are restyled on zoom; detached chunks are restyled when they are
 * attached again.
 */
function refreshEdgeStyle(layer) {
    const scale = edgeWeightScale();

    if (layer.edgeWeightScale === scale) {
        return;
    }

    layer.edgeWeightScale = scale;
    layer.setStyle(layer.options.style);
}


function synchroniseConditionalLayers() {
    if (!state.config) {
        return;
    }

    const sourceShown = sourceLayerShown();

    setLayerPresence(state.sourceLayer.group, sourceShown);

    if (!sourceShown) {
        state.sourceLayer.release();
    }

    for (const bundle of [
        primaryBundle,
        comparisonBundle
    ]) {
        for (const layerName of ALL_SOLUTION_LAYER_NAMES) {
            const layer = bundle.layers[layerName];
            const present = shouldBundleLayerBePresent(
                bundle,
                layerName
            );

            setLayerPresence(layer.group, present);

            if (!present) {
                /*
                 * Hidden layers stop loading and detach their chunks
                 * (closed comparison, labels below their zoom level,
                 * visibility switched off). Built chunk layers are kept
                 * for a quick return.
                 */
                layer.release();
            }
        }
    }

    const primaryVisible = bundleIsVisiblyActive(
        primaryBundle
    );
    const comparisonVisible = bundleIsVisiblyActive(
        comparisonBundle
    );

    const visibilityAvailable = (
        primaryVisible ||
        comparisonVisible
    );

    visibilityToggle.disabled = !visibilityAvailable;

    visibilityToggle.classList.toggle(
        "active",
        state.visibilityWanted && visibilityAvailable
    );

    visibilityToggle.setAttribute(
        "aria-pressed",
        state.visibilityWanted && visibilityAvailable
            ? "true"
            : "false"
    );

    visibilityToggle.textContent = (
        state.visibilityWanted && visibilityAvailable
    )
        ? "Visibility: On"
        : "Visibility: Off";

    /* The button width may have changed. */
    scheduleTopbarLayout();

    if (state.legendElement) {
        for (const row of state.legendElement.querySelectorAll(
            ".legend-source-row"
        )) {
            row.style.display = sourceShown ? "" : "none";
        }

    }
}


function updateBlend() {
    /*
     * The slider runs from the primary solution (left, 0) to the
     * comparison solution (right, 100); its value is the comparison
     * share.
     */
    const comparisonPercentage = clamp(
        Number(solutionFade.value),
        0,
        100
    );

    const primaryPercentage =
        100 - comparisonPercentage;

    const comparisonAvailable = (
        state.overlayOpen &&
        comparisonBundle.ready
    );

    const primaryAlpha = primaryBundle.ready
        ? (
            comparisonAvailable
                ? primaryPercentage / 100
                : 1
        )
        : 0;

    const comparisonAlpha = comparisonAvailable
        ? comparisonPercentage / 100
        : 0;

    setBundleAlpha(primaryBundle, primaryAlpha);
    setBundleAlpha(comparisonBundle, comparisonAlpha);

    /*
     * Split position of shared antenna markers (see updateSharedAntennas):
     * the comparison share, so the border follows the slider's thumb.
     */
    map.getContainer().style.setProperty(
        "--antenna-split",
        `${comparisonPercentage}%`
    );

    /*
     * Coverage labels sit on their point. In comparison mode, they are
     * moved apart (primary up, comparison down) so that labels at the same
     * point stay readable. This holds for every slider position, so the
     * labels do not jump when the slider leaves 0% or 100%.
     */
    map.getContainer().classList.toggle(
        "labels-separated",
        state.overlayOpen
    );

    fadeOutput.textContent = (
        `${primaryPercentage}% primary · ` +
        `${comparisonPercentage}% comparison`
    );

    solutionFade.setAttribute(
        "aria-valuetext",
        `${primaryPercentage} percent primary and ` +
        `${comparisonPercentage} percent comparison`
    );

    if (state.comparisonLegendElement) {
        state.comparisonLegendElement.classList.toggle(
            "is-visible",
            state.overlayOpen
        );
    }

    synchroniseConditionalLayers();
    updateStatisticsHighlights();
}


function updateBasemapOpacity() {
    const opacityPercentage = clamp(
        Number(backgroundOpacity.value),
        0,
        100
    );

    backgroundOpacityOutput.textContent =
        `${opacityPercentage}%`;

    backgroundOpacity.setAttribute(
        "aria-valuetext",
        `${opacityPercentage} percent`
    );

    if (state.tileLayer) {
        state.tileLayer.setOpacity(
            opacityPercentage / 100
        );
    }
}


function updateControlAvailability() {
    const hasPrimaryAlgorithm =
        algorithmSelect.value !== "";

    kSelect.disabled = !hasPrimaryAlgorithm;
    tauSelect.disabled = !hasPrimaryAlgorithm;

    overlayToggle.disabled = (
        !hasPrimaryAlgorithm ||
        !state.config ||
        state.config.algorithms.length === 0
    );

    overlayAlgorithmSelect.disabled =
        !hasPrimaryAlgorithm;
    overlayKSelect.disabled =
        !hasPrimaryAlgorithm;
    overlayTauSelect.disabled =
        !hasPrimaryAlgorithm;

    if (!hasPrimaryAlgorithm) {
        state.visibilityWanted = false;

        deactivateBundle(primaryBundle);
        deactivateBundle(comparisonBundle);

        if (state.overlayOpen) {
            setOverlayOpen(false, false);
        }
    }

    synchroniseConditionalLayers();
}


function chooseInitialComparison() {
    /* The algorithms offered for the selected source. */
    const algorithms = Array.from(overlayAlgorithmSelect.options).map(
        option => option.value
    );

    const optionValues = select => Array.from(select.options).map(
        option => option.value
    );
    const kValues = optionValues(overlayKSelect);
    const tauValues = optionValues(overlayTauSelect);

    const alternativeAlgorithm = algorithms.find(
        algorithm => algorithm !== algorithmSelect.value
    );

    overlayAlgorithmSelect.value = (
        alternativeAlgorithm ||
        algorithmSelect.value ||
        algorithms[0] ||
        ""
    );

    overlayKSelect.value = kValues.includes(kSelect.value)
        ? kSelect.value
        : (kValues[0] || "");

    const alternativeTau = tauValues.find(
        tau => tau !== tauSelect.value
    );

    overlayTauSelect.value = (
        alternativeTau ||
        tauSelect.value ||
        tauValues[0] ||
        ""
    );

    /* Move to available values if this combination has no solution. */
    updateParameterAvailability("comparison");

    solutionFade.value = String(
        clamp(
            Number(
                state.config.map.overlay_initial_blend ?? 50
            ),
            0,
            100
        )
    );
}


function setOverlayOpen(open, loadComparison = true) {
    if (
        open &&
        (
            algorithmSelect.value === "" ||
            overlayToggle.disabled
        )
    ) {
        return;
    }

    state.overlayOpen = Boolean(open);
    overlayPanel.hidden = !state.overlayOpen;

    overlayToggle.classList.toggle(
        "active",
        state.overlayOpen
    );

    overlayToggle.setAttribute(
        "aria-expanded",
        state.overlayOpen ? "true" : "false"
    );

    overlayToggle.textContent = state.overlayOpen
        ? "Compare: On"
        : "Compare: Off";

    layoutTopbar();
    scheduleSharedAntennaUpdate();

    if (
        state.overlayOpen &&
        !state.overlayInitialised
    ) {
        chooseInitialComparison();
        state.overlayInitialised = true;
    }

    if (!state.overlayOpen) {
        /*
         * The comparison keeps its selection and built chunk layers,
         * so reopening it is quick. Its layers leave the map and stop
         * loading (see synchroniseConditionalLayers).
         */
        abortChunkIndex(comparisonBundle.viewportIndex);
        abortChunkIndex(comparisonBundle.preloadIndex);
        chunkLoader.cancelOwner(preloadOwner("comparison"));

        updateBlend();

        if (primaryBundle.ready) {
            state.readyStatusPending = true;
            requestLoadingStateUpdate();
        }
    } else {
        updateBlend();

        if (loadComparison) {
            refreshOverlay();
        }
    }
}


/*
 * Top-bar layout
 *
 * styles.css defines three layout modes (single row, two rows, three
 * rows), plus the split selection for very narrow windows.
 * layoutTopbar() picks the widest mode whose parts fit, based on the
 * widths they need, and then lines up the comparison panel with the
 * fields above it.
 */

const TOPBAR_LAYOUT_CLASSES = [
    "layout-single-row",
    "layout-two-rows",
    "layout-three-rows"
];

const SPLIT_SELECTION_CLASS = "layout-split-selection";

/* Width the status keeps next to the toolbar in the single-row layout. */
const STATUS_MIN_WIDTH_SINGLE_ROW = 240;

/* The comparison algorithm field is never narrower than this. */
const COMPARISON_ALGORITHM_MIN_WIDTH = 90;

const topbarElement = document.querySelector(".topbar");
const topbarMainElement = document.querySelector(".topbar-main");
const brandElement = document.querySelector(".brand");
const selectionGroupElement = document.querySelector(
    ".toolbar-group-selection"
);
const viewGroupElement = document.querySelector(".toolbar-group-view");
const overlayTitleElement = document.querySelector(".overlay-panel-title");

let topbarLayoutFrame = null;

const textMeasureContext = document.createElement("canvas").getContext("2d");


/*
 * Width of an element's text in its font. This works independently of
 * the element's own width, and also while the element is hidden (as the
 * comparison panel is when comparison mode is off).
 */
function textWidth(element) {
    const style = window.getComputedStyle(element);

    textMeasureContext.font = [
        style.fontStyle,
        style.fontWeight,
        style.fontSize,
        style.fontFamily
    ].join(" ");

    return textMeasureContext.measureText(
        element.textContent.trim()
    ).width;
}


function cssPixels(style, property) {
    return parseFloat(style.getPropertyValue(property)) || 0;
}


/*
 * Preferred widths (flex-basis) of the selection fields. They never
 * change, so they are read once, before the split selection (which
 * stretches the source field) can be active.
 */
let selectionFieldBases = null;


/* Preferred width of the selection: the flex-basis of its fields. */
function selectionPreferredWidth(gap) {
    selectionFieldBases ??= Array.from(selectionGroupElement.children).map(
        field => cssPixels(window.getComputedStyle(field), "flex-basis")
    );

    return selectionFieldBases.reduce(
        (sum, width) => sum + width,
        gap * (selectionFieldBases.length - 1)
    );
}


/* Natural width of the view controls: slider plus buttons at text width. */
function viewPreferredWidth(gap) {
    const controls = Array.from(viewGroupElement.children);

    return controls.reduce((sum, control) => {
        const style = window.getComputedStyle(control);

        if (!control.matches("button")) {
            return sum + cssPixels(style, "flex-basis");
        }

        const naturalWidth = (
            textWidth(control) +
            cssPixels(style, "padding-left") +
            cssPixels(style, "padding-right") +
            cssPixels(style, "border-left-width") +
            cssPixels(style, "border-right-width")
        );

        return sum + Math.max(
            naturalWidth,
            cssPixels(style, "min-width")
        );
    }, gap * (controls.length - 1));
}


/* Width the comparison title (swatch and texts) needs. */
function comparisonTitleWidth() {
    const [swatch, titleText] = overlayTitleElement.children;

    return (
        cssPixels(window.getComputedStyle(swatch), "width") +
        cssPixels(
            window.getComputedStyle(overlayTitleElement),
            "column-gap"
        ) +
        Math.max(...Array.from(titleText.children).map(textWidth))
    );
}


/*
 * Narrowest row that still holds the selection fields in one row, in
 * both menus: source, algorithm, k, and tau above, and title, algorithm,
 * k, and tau in the comparison panel. Below it, both menus split at the
 * same time, whether comparison mode is on or not.
 */
function selectionRowMinimumWidth(gap) {
    const minimumWidths = Array.from(selectionGroupElement.children).map(
        field => cssPixels(window.getComputedStyle(field), "min-width")
    );
    const [, , kMinimum, tauMinimum] = minimumWidths;

    const primaryRow = (
        minimumWidths.reduce((sum, width) => sum + width, 0) +
        gap * (minimumWidths.length - 1)
    );
    const comparisonRow = (
        comparisonTitleWidth() +
        COMPARISON_ALGORITHM_MIN_WIDTH +
        kMinimum +
        tauMinimum +
        3 * gap
    );

    return Math.max(primaryRow, comparisonRow);
}


function chooseTopbarLayout() {
    const mainStyle = window.getComputedStyle(topbarMainElement);
    const toolbarGap = cssPixels(
        window.getComputedStyle(selectionGroupElement),
        "column-gap"
    );
    const columnGap = cssPixels(mainStyle, "column-gap");

    const availableWidth = (
        topbarMainElement.clientWidth -
        cssPixels(mainStyle, "padding-left") -
        cssPixels(mainStyle, "padding-right")
    );

    const toolbarWidth = (
        selectionPreferredWidth(toolbarGap) +
        toolbarGap +
        viewPreferredWidth(toolbarGap)
    );

    const singleRowWidth = (
        brandElement.offsetWidth +
        columnGap +
        toolbarWidth +
        columnGap +
        STATUS_MIN_WIDTH_SINGLE_ROW
    );

    if (availableWidth >= singleRowWidth) {
        return {layout: "layout-single-row", splitSelection: false};
    }

    if (availableWidth >= toolbarWidth) {
        return {layout: "layout-two-rows", splitSelection: false};
    }

    return {
        layout: "layout-three-rows",
        splitSelection: (
            availableWidth < selectionRowMinimumWidth(toolbarGap)
        )
    };
}


/*
 * Place the comparison algorithm, k, and tau fields exactly below the
 * primary fields: the grid columns repeat their measured widths, with the
 * same gap as the toolbar (see .overlay-panel.is-aligned in styles.css).
 * If the title needs more room than the space below the source field,
 * the algorithm field starts further right and becomes narrower, so that
 * k and tau still line up. With a split selection, the title has its own
 * row (like the source field), and the fields follow below it.
 */
function alignComparisonPanel(layout, splitSelection) {
    let columns = null;

    if (state.overlayOpen) {
        const fieldRect = select => (
            select.closest(".control").getBoundingClientRect()
        );

        const algorithmRect = fieldRect(algorithmSelect);
        const kRect = fieldRect(kSelect);
        const tauRect = fieldRect(tauSelect);
        const viewRect = viewGroupElement.getBoundingClientRect();

        const panelStyle = window.getComputedStyle(overlayPanel);
        const contentLeft = (
            overlayPanel.getBoundingClientRect().left +
            cssPixels(panelStyle, "padding-left")
        );
        const gap = cssPixels(panelStyle, "column-gap");

        const titleColumn = Math.max(
            comparisonTitleWidth(),
            algorithmRect.left - contentLeft - gap
        );
        const algorithmColumn = (
            algorithmRect.right - (contentLeft + titleColumn + gap)
        );

        if (splitSelection) {
            /* Title row above; algorithm, k, and tau below it. */
            columns = [
                algorithmRect.width,
                kRect.width,
                tauRect.width
            ].map(width => `${width}px`);
        } else if (algorithmColumn >= COMPARISON_ALGORITHM_MIN_WIDTH) {
            const fieldColumns = [
                titleColumn,
                algorithmColumn,
                kRect.width,
                tauRect.width
            ].map(width => `${width}px`);

            if (layout === "layout-single-row") {
                /* Slider below the view controls, Close at the far right. */
                columns = [
                    ...fieldColumns,
                    `${viewRect.width}px`,
                    "minmax(0, 1fr)",
                    "auto"
                ];
            } else if (layout === "layout-two-rows") {
                /* Slider and Close share the view controls' width. */
                columns = [...fieldColumns, "minmax(0, 1fr)"];
            } else {
                /* Slider and Close fill a second row. */
                columns = fieldColumns;
            }
        }
    }

    overlayPanel.classList.toggle("is-aligned", columns !== null);
    overlayPanel.style.gridTemplateColumns = columns
        ? columns.join(" ")
        : "";
}


function layoutTopbar() {
    const {layout, splitSelection} = chooseTopbarLayout();

    if (!topbarElement.classList.contains(layout)) {
        topbarElement.classList.remove(...TOPBAR_LAYOUT_CLASSES);
        topbarElement.classList.add(layout);
    }

    topbarElement.classList.toggle(
        SPLIT_SELECTION_CLASS,
        splitSelection
    );

    alignComparisonPanel(layout, splitSelection);
    updateStatusOverflow();
}


/* Lay out at most once per frame, for example while resizing. */
function scheduleTopbarLayout() {
    if (topbarLayoutFrame !== null) {
        return;
    }

    topbarLayoutFrame = window.requestAnimationFrame(() => {
        topbarLayoutFrame = null;
        layoutTopbar();
    });
}


layoutTopbar();

document.fonts?.ready.then(scheduleTopbarLayout);


function sourceLabel(sourceId = sourceSelect.value) {
    if (!state.config) {
        return sourceId;
    }

    const source = state.config.sources.find(
        candidate => candidate.id === sourceId
    );

    return source?.label || sourceId;
}


function updateStatisticsHeading() {
    const label = sourceLabel();

    instanceInformationTitle.textContent =
        "Instance Statistics";

    instanceSourceName.textContent = label || "—";

    instanceInformation.setAttribute(
        "aria-label",
        label
            ? `Instance statistics for ${label}`
            : "Instance statistics"
    );
}


function polygonCountText() {
    if (
        state.statistics &&
        state.statistics.source === sourceSelect.value
    ) {
        return Number(
            state.statistics.total_polygons
        ).toLocaleString();
    }

    return null;
}


function primaryStatusMessage() {
    const count = polygonCountText();

    if (count !== null) {
        return (
            `Primary solution loaded for ` +
            `${count} polygons.`
        );
    }

    return "Primary solution loaded.";
}


function comparisonStatusMessage() {
    const count = polygonCountText();

    if (count !== null) {
        return (
            "Primary and comparison solutions loaded " +
            `for ${count} polygons.`
        );
    }

    return "Primary and comparison solutions loaded.";
}


function sourceExtentReady(sourceId) {
    return Boolean(
        state.sourceExtent &&
        state.sourceExtentId === sourceId
    );
}


/*
 * Attached chunks stay on the map while they overlap this area; see
 * RETAIN_PADDING.
 */
function retainBbox() {
    return leafletBoundsToBbox(
        map.getBounds().pad(RETAIN_PADDING)
    );
}


function prepareSourceLayer(sourceId) {
    if (state.sourceId === sourceId) {
        return;
    }

    state.sourceId = sourceId;

    resetChunkIndex(state.sourceIndex);
    resetChunkIndex(state.sourcePreloadIndex);
    chunkLoader.cancelOwner(preloadOwner("source"));

    state.sourceLayer.clear();
}


async function refreshSourceViewport(viewport) {
    const sourceId = sourceSelect.value;

    if (!sourceExtentReady(sourceId)) {
        return;
    }

    prepareSourceLayer(sourceId);

    if (!viewport.bbox || !sourceLayerShown()) {
        state.sourceLayer.release();
        return;
    }

    const generation = state.sourceLayer.generation;

    const payload = await loadChunkIndex(
        state.sourceIndex,
        sourceChunkIndexUrl(sourceId, viewport.bbox)
    );

    if (
        generation !== state.sourceLayer.generation ||
        sourceSelect.value !== sourceId
    ) {
        return;
    }

    state.sourceLayer.sync(
        chunkDescriptorsByLayer(payload).get("source") || [],
        retainBbox(),
        0,
        payload.version
    );
}


async function refreshBundleViewport(bundle, viewport, orderBias) {
    if (
        !bundleIsShown(bundle) ||
        !bundle.selection ||
        !sourceExtentReady(bundle.selection.source)
    ) {
        return;
    }

    const layerNames = presentBundleLayerNames(bundle);

    if (!viewport.bbox || layerNames.length === 0) {
        for (const layerName of ALL_SOLUTION_LAYER_NAMES) {
            bundle.layers[layerName].release();
        }

        return;
    }

    const generation = bundle.generation;

    const payload = await loadChunkIndex(
        bundle.viewportIndex,
        solutionChunkIndexUrl(
            bundle.selection,
            layerNames,
            viewport.bbox
        )
    );

    if (generation !== bundle.generation) {
        return;
    }

    /* Several runs of the selection: shown as a warning when loaded. */
    bundle.runs = payload.runs ?? null;

    const descriptorsByLayer = chunkDescriptorsByLayer(payload);
    const retain = retainBbox();

    for (const layerName of layerNames) {
        /* A layer may have been hidden while the index was loading. */
        if (!shouldBundleLayerBePresent(bundle, layerName)) {
            continue;
        }

        bundle.layers[layerName].sync(
            descriptorsByLayer.get(API_LAYER_NAMES[layerName]) || [],
            retain,
            orderBias,
            payload.version
        );
    }
}


function refreshViewportData() {
    window.clearTimeout(state.viewportRefreshTimer);
    state.viewportRefreshTimer = null;

    const viewport = currentViewportRequest();

    refreshSourceViewport(viewport).catch(error => {
        reportLoadingError(
            error,
            "Source buildings could not be loaded: "
        );
    });

    refreshBundleViewport(
        primaryBundle,
        viewport,
        0
    ).catch(error => {
        reportLoadingError(error);
    });

    if (state.overlayOpen) {
        refreshBundleViewport(
            comparisonBundle,
            viewport,
            COMPARISON_ORDER_OFFSET
        ).catch(error => {
            reportLoadingError(
                error,
                "The comparison could not be loaded: "
            );
        });
    }

    requestLoadingStateUpdate();
}


function scheduleViewportRefresh() {
    window.clearTimeout(
        state.viewportRefreshTimer
    );

    state.viewportRefreshTimer = window.setTimeout(
        refreshViewportData,
        VIEWPORT_REFRESH_DELAY
    );
}


/*
 * Background preloading: once the current view is complete, fetch all
 * remaining chunks of the shown layers, nearest to the map centre first,
 * into the chunk store. They are turned into map layers only when the
 * view reaches them.
 */
function schedulePreload() {
    window.clearTimeout(state.preloadTimer);

    state.preloadTimer = window.setTimeout(
        runPreload,
        PRELOAD_DELAY
    );
}


function runPreload() {
    state.preloadTimer = null;

    /* Rescheduled by updateLoadingState once the view is idle again. */
    if (isViewportLoading()) {
        return;
    }

    const dataExtent = currentDataExtent();
    const sourceId = sourceSelect.value;

    if (!dataExtent || !sourceExtentReady(sourceId)) {
        return;
    }

    if (state.sourceId === sourceId && sourceLayerShown()) {
        const generation = state.sourceLayer.generation;

        preloadChunks(
            state.sourcePreloadIndex,
            sourceChunkIndexUrl(sourceId, dataExtent),
            preloadOwner("source"),
            () => generation === state.sourceLayer.generation,
            0
        );
    }

    for (const bundle of [primaryBundle, comparisonBundle]) {
        if (!bundleIsShown(bundle) || !bundle.selection) {
            continue;
        }

        const layerNames = presentBundleLayerNames(bundle);

        if (layerNames.length === 0) {
            continue;
        }

        const generation = bundle.generation;

        preloadChunks(
            bundle.preloadIndex,
            solutionChunkIndexUrl(
                bundle.selection,
                layerNames,
                dataExtent
            ),
            preloadOwner(bundle.role),
            () => generation === bundle.generation,
            bundle.role === "primary"
                ? 0
                : COMPARISON_ORDER_OFFSET
        );
    }
}


async function preloadChunks(
    indexState,
    url,
    owner,
    isCurrent,
    orderBias
) {
    /* The same centre and layers were already queued. */
    if (indexState.lastUrl === url) {
        return;
    }

    try {
        const payload = await loadChunkIndex(
            indexState,
            url,
            true
        );

        if (!isCurrent()) {
            return;
        }

        /* Replace the previous queue, which used an older centre. */
        chunkLoader.cancelOwner(owner);

        payload.chunks.forEach((chunk, position) => {
            const url = chunkUrl(payload, chunk);

            if (chunkStore.has(url)) {
                return;
            }

            chunkLoader.request(url, {
                owner,
                foreground: false,
                order: orderBias + position,
                bytes: chunk.bytes
            }).catch(error => {
                if (error?.name !== "AbortError") {
                    console.debug(
                        "Could not preload a map data chunk:",
                        error
                    );
                }
            });
        });
    } catch (error) {
        if (error?.name !== "AbortError") {
            console.warn("Background preloading failed:", error);
        }
    }
}


async function refreshPrimary() {
    if (!state.config) {
        return;
    }

    const requestId = ++state.mainRequestId;
    const sourceId = sourceSelect.value;

    state.staleChunkRetries = 0;

    updateStatisticsHeading();
    updateControlAvailability();

    setStatus(
        "Locating the selected source instance…",
        "loading"
    );

    try {
        await ensureSourceExtentAndFit(sourceId);
    } catch (error) {
        if (error.name === "AbortError") {
            return;
        }

        if (requestId !== state.mainRequestId) {
            return;
        }

        setStatus(error.message, "error");
        console.error(error);
        return;
    }

    if (
        requestId !== state.mainRequestId ||
        sourceSelect.value !== sourceId
    ) {
        return;
    }

    if (algorithmSelect.value === "") {
        deactivateBundle(primaryBundle);

        setStatus(
            "Loading source buildings for the current map view…",
            "loading"
        );
    } else {
        prepareBundleSelection(
            primaryBundle,
            makeSelection(
                algorithmSelect.value,
                kSelect.value,
                tauSelect.value
            )
        );

        /*
         * The comparison always uses the primary source, so it changes
         * together with the source.
         */
        if (state.overlayOpen) {
            prepareBundleSelection(
                comparisonBundle,
                comparisonSelection()
            );
        }

        setStatus(
            "Loading primary solution for the current map view…",
            "loading"
        );
    }

    state.readyStatusPending = true;
    refreshViewportData();
}


function comparisonSelection() {
    return makeSelection(
        overlayAlgorithmSelect.value,
        overlayKSelect.value,
        overlayTauSelect.value
    );
}


function refreshOverlay() {
    if (
        !state.overlayOpen ||
        algorithmSelect.value === ""
    ) {
        return;
    }

    prepareBundleSelection(
        comparisonBundle,
        comparisonSelection()
    );

    setStatus(
        "Loading comparison solution for the current map view…",
        "loading"
    );

    state.readyStatusPending = true;
    refreshViewportData();
}


function createTableCell(text, className = "") {
    const cell = document.createElement("td");
    cell.textContent = text;

    if (className) {
        cell.className = className;
    }

    return cell;
}


function equivalentConfiguredValue(
    selectElement,
    requestedValue
) {
    const requestedText = String(requestedValue);
    const options = Array.from(selectElement.options);

    const exact = options.find(
        option => option.value === requestedText
    );

    if (exact) {
        return exact.value;
    }

    const numericRequested = Number(requestedText);

    if (Number.isFinite(numericRequested)) {
        const numericMatch = options.find(option => (
            Number.isFinite(Number(option.value)) &&
            Number(option.value) === numericRequested
        ));

        if (numericMatch) {
            return numericMatch.value;
        }
    }

    return null;
}


function statisticsSelectionKey(
    source,
    algorithm,
    k,
    tau
) {
    return selectionKey({
        source,
        algorithm,
        k,
        tau
    });
}


function createStatisticsSolutionCell(
    statistics,
    statisticsRow,
    algorithm,
    result
) {
    if (!result) {
        return createTableCell(
            "—",
            "statistics-empty-cell"
        );
    }

    if (
        result.status !== "available" ||
        result.count === null
    ) {
        const cell = createTableCell(
            "Incomplete",
            "statistics-incomplete-cell"
        );

        const missing = Array.isArray(result.missing)
            ? result.missing.join(", ")
            : "";

        cell.title = missing
            ? `${result.directory}; missing: ${missing}`
            : result.directory;

        return cell;
    }

    const hasMultipleRuns = result.runs > 1;
    const cell = document.createElement("td");
    const button = document.createElement("button");

    cell.className = "statistics-solution-cell";

    cell.dataset.selectionKey = statisticsSelectionKey(
        statistics.source,
        algorithm.id,
        statisticsRow.k,
        statisticsRow.tau
    );

    button.type = "button";
    button.className = "statistics-solution-button";
    button.textContent = (
        Number(result.count).toLocaleString() +
        (hasMultipleRuns ? "*" : "")
    );

    button.dataset.algorithm = algorithm.id;
    button.dataset.k = statisticsRow.k;
    button.dataset.tau = statisticsRow.tau;

    button.setAttribute(
        "aria-label",
        (
            `Show ${algorithm.label}, ` +
            `k ${statisticsRow.k}, ` +
            `tau ${statisticsRow.tau} as primary solution`
        )
    );

    const runDescription = hasMultipleRuns
        ? `best of ${result.runs} runs`
        : "single run";

    cell.title = (
        `${result.directory}; ${runDescription}; ` +
        "left-click for primary, right-click for comparison " +
        "(again to turn comparison off)"
    );

    cell.appendChild(button);

    return cell;
}


function compareParameterValues(first, second) {
    const firstNumber = Number(first);
    const secondNumber = Number(second);

    if (
        Number.isFinite(firstNumber) &&
        Number.isFinite(secondNumber)
    ) {
        return firstNumber - secondNumber;
    }

    return String(first).localeCompare(String(second));
}


/*
 * k and tau selectors
 *
 * The choices are the configured values plus, with discovered solutions,
 * the values found for the selected source (taken from the statistics).
 * Choices without a solution for the selected algorithm are disabled,
 * which greys them out in the normal drop-down menus. When a change of
 * the algorithm or of k leaves the current choice without a solution, the
 * nearest available value is selected instead.
 */
const PARAMETER_SELECTORS = {
    primary: {
        algorithm: algorithmSelect,
        k: kSelect,
        tau: tauSelect
    },
    comparison: {
        algorithm: overlayAlgorithmSelect,
        k: overlayKSelect,
        tau: overlayTauSelect
    }
};


/*
 * Solutions of the selected source: algorithm ID -> k -> set of tau
 * values (as numbers). Null while the statistics are not loaded.
 */
function availableParameters() {
    const statistics = state.statistics;

    if (!statistics || statistics.source !== sourceSelect.value) {
        return null;
    }

    const result = new Map();

    for (const row of statistics.rows) {
        for (const [algorithmId, cell] of Object.entries(row.cells)) {
            if (cell.status !== "available") {
                continue;
            }

            if (!result.has(algorithmId)) {
                result.set(algorithmId, new Map());
            }

            const byK = result.get(algorithmId);

            if (!byK.has(row.k)) {
                byK.set(row.k, new Set());
            }

            byK.get(row.k).add(Number(row.tau));
        }
    }

    return result;
}


/* Values without numeric duplicates ("0.5" and "0.50"), sorted. */
function uniqueParameterValues(values) {
    const result = [];

    for (const value of values.map(String)) {
        const isDuplicate = result.some(existing => (
            existing === value ||
            (
                Number.isFinite(Number(existing)) &&
                Number(existing) === Number(value)
            )
        ));

        if (!isDuplicate) {
            result.push(value);
        }
    }

    return result.sort(compareParameterValues);
}


/* Replace a selector's options, keeping its value where possible. */
function replaceOptions(selectElement, values) {
    const current = selectElement.value;
    const existing = Array.from(selectElement.options).map(
        option => option.value
    );

    if (
        existing.length === values.length &&
        existing.every((value, index) => value === values[index])
    ) {
        return;
    }

    selectElement.replaceChildren();
    addOptions(selectElement, values);

    const kept = equivalentConfiguredValue(selectElement, current);

    if (kept !== null) {
        selectElement.value = kept;
    }
}


function rebuildParameterOptions() {
    /* The values configured for the selected source. */
    const source = state.config.sources.find(
        candidate => candidate.id === sourceSelect.value
    );
    let kValues = source?.k_values ?? [];
    let tauValues = source?.tau_values ?? [];

    if (
        state.config.discovery?.solutions &&
        state.statistics?.source === sourceSelect.value
    ) {
        kValues = kValues.concat(
            state.statistics.rows.map(row => row.k)
        );
        tauValues = tauValues.concat(
            state.statistics.rows.map(row => row.tau)
        );
    }

    kValues = uniqueParameterValues(kValues);
    tauValues = uniqueParameterValues(tauValues);

    for (const selectors of Object.values(PARAMETER_SELECTORS)) {
        replaceOptions(selectors.k, kValues);
        replaceOptions(selectors.tau, tauValues);
    }
}


/* The candidate closest to the current value (numerically if possible). */
function nearestParameterValue(current, candidates) {
    const sorted = [...candidates].sort(compareParameterValues);
    const currentNumber = Number(current);

    if (!Number.isFinite(currentNumber)) {
        return sorted[0];
    }

    return sorted.reduce((best, candidate) => (
        Math.abs(Number(candidate) - currentNumber) <
            Math.abs(Number(best) - currentNumber)
            ? candidate
            : best
    ));
}


/*
 * Grey out k and tau choices without a solution for the selected
 * algorithm, and move an unavailable selection to the nearest available
 * values. Returns true if the selection was changed.
 */
function updateParameterAvailability(role) {
    const {algorithm, k, tau} = PARAMETER_SELECTORS[role];
    const available = availableParameters();

    if (available === null || algorithm.value === "") {
        for (const option of [...k.options, ...tau.options]) {
            option.disabled = false;
        }

        return false;
    }

    const byK = available.get(algorithm.value) ?? new Map();
    let changed = false;

    for (const option of k.options) {
        option.disabled = !byK.has(option.value);
    }

    if (!byK.has(k.value) && byK.size > 0) {
        const enabledK = Array.from(k.options)
            .filter(option => !option.disabled)
            .map(option => option.value);

        if (enabledK.length > 0) {
            k.value = nearestParameterValue(k.value, enabledK);
            changed = true;
        }
    }

    const taus = byK.get(k.value) ?? new Set();

    for (const option of tau.options) {
        option.disabled = !taus.has(Number(option.value));
    }

    if (!taus.has(Number(tau.value))) {
        const enabledTau = Array.from(tau.options)
            .filter(option => !option.disabled)
            .map(option => option.value);

        if (enabledTau.length > 0) {
            tau.value = nearestParameterValue(tau.value, enabledTau);
            changed = true;
        }
    }

    return changed;
}


/*
 * After new statistics: rebuild the choices and reload a solution whose
 * selection had to move to available values.
 */
/*
 * With discovered solutions, offer only the algorithms that have a
 * solution for the selected source (the statistics list exactly these).
 * An algorithm that is no longer offered is replaced by the first offered
 * one. Returns which selections changed.
 */
function rebuildAlgorithmOptions() {
    const changed = {primary: false, comparison: false};

    if (
        !state.config.discovery?.solutions ||
        state.statistics?.source !== sourceSelect.value
    ) {
        return changed;
    }

    const offered = state.statistics.algorithms;
    const offeredIds = offered.map(algorithm => algorithm.id);

    for (const [role, select, noneOption] of [
        ["primary", algorithmSelect, true],
        ["comparison", overlayAlgorithmSelect, false]
    ]) {
        const current = select.value;
        const values = [
            ...(noneOption ? [""] : []),
            ...offeredIds
        ];
        const existing = Array.from(select.options).map(
            option => option.value
        );

        if (
            existing.length !== values.length ||
            existing.some((value, index) => value !== values[index])
        ) {
            select.replaceChildren();

            if (noneOption) {
                const option = document.createElement("option");

                option.value = "";
                option.textContent = "No algorithm";
                select.appendChild(option);
            }

            addOptions(select, offered);
        }

        if (current === "" && noneOption) {
            select.value = "";
        } else if (offeredIds.includes(current)) {
            select.value = current;
        } else {
            select.value = offeredIds[0] ?? "";
        }

        changed[role] = select.value !== current;
    }

    return changed;
}


function applyParameterAvailability() {
    const algorithmsChanged = rebuildAlgorithmOptions();

    rebuildParameterOptions();

    const primaryChanged = (
        updateParameterAvailability("primary") ||
        algorithmsChanged.primary
    );
    const comparisonChanged = (
        updateParameterAvailability("comparison") ||
        algorithmsChanged.comparison
    );

    if (primaryChanged) {
        refreshPrimary();
    } else if (comparisonChanged && state.overlayOpen) {
        refreshOverlay();
    }
}


function renderStatistics(statistics) {
    state.statistics = statistics;
    applyParameterAvailability();

    const formattedPolygonCount = Number(
        statistics.total_polygons
    ).toLocaleString();

    instancePolygonCount.textContent =
        formattedPolygonCount;

    instanceInformationSummary.textContent =
        `${formattedPolygonCount} polygons`;

    instanceInformationTitle.textContent =
    "Instance Statistics";

    instanceSourceName.textContent =
        statistics.source_label || sourceLabel() || "—";

    const tableHead =
        solutionStatisticsTable.querySelector("thead");
    const tableBody =
        solutionStatisticsTable.querySelector("tbody");

    tableHead.replaceChildren();
    tableBody.replaceChildren();

    const headingRow = document.createElement("tr");

    for (const heading of ["k", "Tau"]) {
        const cell = document.createElement("th");

        cell.scope = "col";
        cell.textContent = heading;
        cell.className = "statistics-parameter-heading";

        headingRow.appendChild(cell);
    }

    for (const algorithm of statistics.algorithms) {
        const cell = document.createElement("th");

        cell.scope = "col";
        /* The table is narrow: a shorter label, the full one on hover. */
        cell.textContent = algorithm.short_label ?? algorithm.label;
        cell.title = algorithm.label;
        cell.className = "statistics-algorithm-heading";

        headingRow.appendChild(cell);
    }

    tableHead.appendChild(headingRow);

    if (statistics.rows.length === 0) {
        const row = document.createElement("tr");
        const cell = createTableCell(
            "No solutions found",
            "statistics-empty-cell"
        );

        cell.colSpan = 2 + statistics.algorithms.length;

        row.appendChild(cell);
        tableBody.appendChild(row);

        statisticsFootnote.hidden = true;
        updateStatisticsHighlights();

        return;
    }

    let multipleRunsExist = false;

    const sortedRows = [...statistics.rows].sort(
        (first, second) => (
            compareParameterValues(first.k, second.k) ||
            compareParameterValues(first.tau, second.tau)
        )
    );

    for (const statisticsRow of sortedRows) {
        const row = document.createElement("tr");

        row.appendChild(
            createTableCell(
                statisticsRow.k,
                "statistics-parameter-cell"
            )
        );

        row.appendChild(
            createTableCell(
                statisticsRow.tau,
                "statistics-parameter-cell"
            )
        );

        for (const algorithm of statistics.algorithms) {
            const result =
                statisticsRow.cells[algorithm.id];

            if (result?.runs > 1) {
                multipleRunsExist = true;
            }

            row.appendChild(
                createStatisticsSolutionCell(
                    statistics,
                    statisticsRow,
                    algorithm,
                    result
                )
            );
        }

        tableBody.appendChild(row);
    }

    statisticsFootnote.hidden = !multipleRunsExist;
    updateStatisticsHighlights();
}


function updateStatisticsHighlights() {
    const primaryKey = primaryBundle.ready
        ? primaryBundle.key
        : null;

    const comparisonKey = (
        state.overlayOpen &&
        comparisonBundle.ready
    )
        ? comparisonBundle.key
        : null;

    const cells = solutionStatisticsTable.querySelectorAll(
        "td[data-selection-key]"
    );

    for (const cell of cells) {
        const cellKey = cell.dataset.selectionKey;

        const isPrimary = Boolean(
            primaryKey &&
            cellKey === primaryKey
        );

        const isComparison = Boolean(
            comparisonKey &&
            cellKey === comparisonKey
        );

        cell.classList.toggle(
            "is-primary-solution",
            isPrimary
        );

        cell.classList.toggle(
            "is-comparison-solution",
            isComparison
        );

        const button = cell.querySelector("button");

        if (button) {
            const descriptions = [];

            if (isPrimary) {
                descriptions.push(
                    "currently displayed as primary"
                );
            }

            if (isComparison) {
                descriptions.push(
                    "currently displayed as comparison"
                );
            }

            button.setAttribute(
                "aria-current",
                descriptions.length > 0
                    ? "true"
                    : "false"
            );

            button.dataset.displayState =
                descriptions.join(", ");
        }
    }
}


function applyStatisticsSelection(
    button,
    targetRole = "primary"
) {
    const isComparison = targetRole === "comparison";

    if (
        isComparison &&
        algorithmSelect.value === ""
    ) {
        setStatus(
            "Select a primary solution before selecting " +
            "a comparison solution.",
            "error"
        );

        return;
    }

    /* Right-clicking the current comparison again turns comparison off. */
    if (
        isComparison &&
        state.overlayOpen &&
        comparisonBundle.ready &&
        button.closest("td")?.dataset.selectionKey ===
            comparisonBundle.key
    ) {
        setOverlayOpen(false);
        return;
    }

    const algorithm = button.dataset.algorithm;

    const targetAlgorithmSelect = isComparison
        ? overlayAlgorithmSelect
        : algorithmSelect;

    const targetKSelect = isComparison
        ? overlayKSelect
        : kSelect;

    const targetTauSelect = isComparison
        ? overlayTauSelect
        : tauSelect;

    const kValue = equivalentConfiguredValue(
        targetKSelect,
        button.dataset.k
    );

    const tauValue = equivalentConfiguredValue(
        targetTauSelect,
        button.dataset.tau
    );

    const algorithmExists = Array.from(
        targetAlgorithmSelect.options
    ).some(option => option.value === algorithm);

    if (
        !algorithm ||
        !algorithmExists ||
        kValue === null ||
        tauValue === null
    ) {
        setStatus(
            "This statistics entry is not available in " +
            "the current configuration.",
            "error"
        );

        return;
    }

    targetAlgorithmSelect.value = algorithm;
    targetKSelect.value = kValue;
    targetTauSelect.value = tauValue;

    updateParameterAvailability(isComparison ? "comparison" : "primary");

    if (isComparison) {
        state.overlayInitialised = true;

        if (!state.overlayOpen) {
            setOverlayOpen(true, false);
        }

        refreshOverlay();
        return;
    }

    updateControlAvailability();
    refreshPrimary();
}


/*
 * background: refresh after new solutions were found. The current table
 * and selector choices stay until the new statistics arrive.
 */
async function refreshStatistics(background = false) {
    if (!sourceSelect.value) {
        return;
    }

    if (state.statisticsController) {
        state.statisticsController.abort();
    }

    const controller = new AbortController();
    const requestId = ++state.statisticsRequestId;
    const sourceId = sourceSelect.value;

    state.statisticsController = controller;

    if (!background) {
        state.statistics = null;

        updateStatisticsHeading();

        instancePolygonCount.textContent = "—";
        instanceInformationSummary.textContent =
            "Loading statistics…";
    }

    try {
        const statistics = await fetchJson(
            `/api/source/${
                encodeURIComponent(sourceId)
            }/statistics`,
            controller.signal
        );

        if (
            requestId !== state.statisticsRequestId ||
            sourceSelect.value !== sourceId
        ) {
            return;
        }

        renderStatistics(statistics);
    } catch (error) {
        if (error.name === "AbortError") {
            return;
        }

        console.error(error);

        /* A failed background refresh keeps the current table. */
        if (background && state.statistics) {
            return;
        }

        instanceInformationSummary.textContent =
            "Statistics unavailable";
        instancePolygonCount.textContent = "—";
    } finally {
        if (
            state.statisticsController === controller
        ) {
            state.statisticsController = null;
        }
    }
}


/*
 * Keeping the statistics current
 *
 * The server reports a version of its solution directories with the
 * statistics, every chunk index, and /api/solutions/version. When a
 * response carries a different version than the displayed statistics, a
 * solution was added, removed, or renamed, and the statistics (and with
 * them the selector choices) are reloaded in the background.
 */
const CATALOG_CHECK_MIN_INTERVAL_MS = 3000;

let lastCatalogCheck = 0;


function checkCatalogVersion(version) {
    if (
        typeof version !== "string" ||
        !state.statistics ||
        state.statistics.source !== sourceSelect.value ||
        state.statistics.catalog_version === version ||
        state.statisticsController
    ) {
        return;
    }

    refreshStatistics(true);
}


/*
 * Before the user picks a solution, ask the server whether new solutions
 * exist, so that recently added ones become selectable.
 */
function checkCatalogBeforeSelection() {
    const now = Date.now();

    if (now - lastCatalogCheck < CATALOG_CHECK_MIN_INTERVAL_MS) {
        return;
    }

    lastCatalogCheck = now;

    fetchJson("/api/solutions/version")
        .then(payload => checkCatalogVersion(payload.catalog_version))
        .catch(error => console.debug(
            "Could not check for new solutions:",
            error
        ));
}


for (const select of [
    algorithmSelect,
    kSelect,
    tauSelect,
    overlayAlgorithmSelect,
    overlayKSelect,
    overlayTauSelect
]) {
    select.addEventListener("focus", checkCatalogBeforeSelection);
    select.addEventListener("pointerdown", checkCatalogBeforeSelection);
}


async function initialise() {
    beginLoading();

    try {
        const config = await fetchJson("/api/config");

        state.config = config;

        applicationTitle.textContent = config.title;
        document.title = config.title;

        addOptions(sourceSelect, config.sources);
        addOptions(algorithmSelect, config.algorithms);
        addOptions(
            overlayAlgorithmSelect,
            config.algorithms
        );

        /* Extended by discovered values once the statistics arrive. */
        rebuildParameterOptions();

        map.setView(
            config.map.center,
            config.map.zoom
        );
        
        updateZoomDependentStyles();

        const initialBackgroundOpacity = clamp(
            Number(
                config.map.background_opacity ?? 1
            ),
            0,
            1
        );

        backgroundOpacity.value = String(
            Math.round(
                initialBackgroundOpacity * 100
            )
        );

        solutionFade.value = String(
            clamp(
                Number(
                    config.map.overlay_initial_blend ?? 50
                ),
                0,
                100
            )
        );

        state.tileLayer = L.tileLayer(
            config.map.tile_url,
            {
                attribution: config.map.attribution,
                maxZoom: config.map.max_zoom,
                maxNativeZoom: Number(
                    config.map.tile_max_native_zoom ??
                    config.map.max_zoom
                ),
                opacity: initialBackgroundOpacity,
                keepBuffer: 4,
                updateWhenIdle: true
            }
        ).addTo(map);

        state.tileLayer.on("tileerror", event => {
            console.debug(
                "A background-map tile could not be loaded.",
                event
            );
        });

        updateStatisticsHeading();
        updateBasemapOpacity();
        updateBlend();
        updateControlAvailability();

        /* The configured title and options change the widths. */
        layoutTopbar();

        refreshStatistics();
        await refreshPrimary();
    } catch (error) {
        setStatus(error.message, "error");
        console.error(error);
    } finally {
        finishLoading();
    }
}


sourceSelect.addEventListener("change", () => {
    /*
     * Invalidate all pending requests immediately. The data of the
     * previous source is cleared as well: the map moves to the new
     * source, and old features must not stay visible if loading fails.
     */
    state.mainRequestId += 1;
    state.sourceExtentRequestId += 1;

    if (state.sourceExtentController) {
        state.sourceExtentController.abort();
        state.sourceExtentController = null;
    }

    state.sourceExtent = null;
    state.sourceExtentId = null;
    state.sourceExtentPendingKey = null;
    state.sourceExtentPendingPromise = null;
    state.sourceFittedId = null;

    state.overlayInitialised = false;

    /*
     * Without discovered solutions, only the source's configured k and tau
     * values can be requested, so switch to them before loading. With
     * discovered solutions, the statistics of the new source update the
     * choices once they arrive.
     */
    if (!state.config.discovery?.solutions) {
        rebuildParameterOptions();
    }

    prepareSourceLayer(sourceSelect.value);

    if (algorithmSelect.value !== "") {
        prepareBundleSelection(
            primaryBundle,
            makeSelection(
                algorithmSelect.value,
                kSelect.value,
                tauSelect.value
            )
        );
    }

    if (state.overlayOpen) {
        prepareBundleSelection(
            comparisonBundle,
            comparisonSelection()
        );
    }

    refreshStatistics();
    refreshPrimary();
});


algorithmSelect.addEventListener("change", () => {
    if (
        algorithmSelect.value === "" &&
        state.overlayOpen
    ) {
        setOverlayOpen(false, false);
    }

    updateParameterAvailability("primary");
    updateControlAvailability();
    refreshPrimary();
});


/*
 * A new algorithm or k may leave the current tau (or k) without a
 * solution; updateParameterAvailability then moves it to the nearest
 * available value before loading.
 */
for (const select of [kSelect, tauSelect]) {
    select.addEventListener("change", () => {
        updateParameterAvailability("primary");
        refreshPrimary();
    });
}

for (const select of [
    overlayAlgorithmSelect,
    overlayKSelect,
    overlayTauSelect
]) {
    select.addEventListener("change", () => {
        updateParameterAvailability("comparison");
        refreshOverlay();
    });
}


overlayToggle.addEventListener("click", () => {
    setOverlayOpen(!state.overlayOpen);
});


closeOverlayButton.addEventListener("click", () => {
    setOverlayOpen(false);
});


/* Fading only changes pane opacities; no map data is reloaded. */
solutionFade.addEventListener("input", updateBlend);


backgroundOpacity.addEventListener(
    "input",
    updateBasemapOpacity
);


visibilityToggle.addEventListener("click", () => {
    if (visibilityToggle.disabled) {
        return;
    }

    state.visibilityWanted =
        !state.visibilityWanted;

    synchroniseConditionalLayers();
    refreshViewportData();
});


/*
 * The asynchronous clipboard API requires a secure context (HTTPS or
 * localhost); the fallback also works when the viewer is opened through
 * plain HTTP on another machine.
 */
async function copyTextToClipboard(text) {
    if (navigator.clipboard && window.isSecureContext) {
        await navigator.clipboard.writeText(text);
        return;
    }

    const textarea = document.createElement("textarea");

    textarea.value = text;
    textarea.setAttribute("readonly", "");
    textarea.style.position = "fixed";
    textarea.style.opacity = "0";

    document.body.appendChild(textarea);
    textarea.select();

    try {
        if (!document.execCommand("copy")) {
            throw new Error("The browser refused to copy.");
        }
    } finally {
        textarea.remove();
    }
}


/* Any status message can be copied by click or keyboard. */
function copyStatus() {
    copyTextToClipboard(
        statusTextElement.textContent.trim()
    ).then(() => {
        statusElement.classList.add("is-copied");

        window.clearTimeout(state.statusCopiedTimer);

        state.statusCopiedTimer = window.setTimeout(() => {
            statusElement.classList.remove("is-copied");
        }, 1800);
    }).catch(error => {
        console.warn("Could not copy the status message:", error);
    });
}


statusElement.tabIndex = 0;
statusElement.addEventListener("click", copyStatus);

statusTextElement.addEventListener("scroll", updateStatusOverflow);

statusElement.addEventListener("keydown", event => {
    if (event.key === "Enter" || event.key === " ") {
        event.preventDefault();
        copyStatus();
    }
});


instanceInformationToggle.addEventListener(
    "click",
    () => {
        const collapsed =
            instanceInformation.classList.toggle(
                "is-collapsed"
            );

        instanceInformationToggle.setAttribute(
            "aria-expanded",
            collapsed ? "false" : "true"
        );
    }
);


solutionStatisticsTable.addEventListener(
    "click",
    event => {
        const button = event.target.closest(
            ".statistics-solution-button"
        );

        if (!button) {
            return;
        }

        applyStatisticsSelection(
            button,
            "primary"
        );
    }
);


solutionStatisticsTable.addEventListener(
    "contextmenu",
    event => {
        const button = event.target.closest(
            ".statistics-solution-button"
        );

        if (!button) {
            return;
        }

        event.preventDefault();

        applyStatisticsSelection(
            button,
            "comparison"
        );
    }
);


map.on("zoomend", () => {
    updateZoomDependentStyles();
    synchroniseConditionalLayers();
    scheduleViewportRefresh();
});


map.on(
    "moveend",
    scheduleViewportRefresh
);


/*
 * The map follows its container size on its own (see the observer next
 * to the map setup); only the top bar needs a new layout.
 */
window.addEventListener("resize", scheduleTopbarLayout);


initialise();
