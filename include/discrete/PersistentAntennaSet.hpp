#pragma once

// TODO refactor away by moving Coverage to header
// related TODO drop min from coverage and instead always pass as param?
#include "AntennaSet.hpp"
#include "Interval.hpp"

#include <bit>
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>
#include <variant>
#include <algorithm>

template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };


template<class F1, class F2, class F3>
struct UpdateFuncs {
    F1 modify_fn; // (V base, V override) -> std::optional<V>
    F2 agg_init_fn; // (V v) -> A
    F3 agg_join_fn; // (A a1, A a2) -> A
};

template<class K, class V, class A>
struct PersistentMapNode {
    static_assert(std::is_unsigned_v<K>, "keys must be unsigned integers");

    using Children = std::array<std::shared_ptr<PersistentMapNode>, 2>;

    uint8_t crit_bit = 0;
    K common_prefix = 0;
    A aggregate;

    struct Inner { Children children; };
    struct Leaf { V value; };

    using Content = std::variant<Inner, Leaf>;
    Content content;

public:

    const A &getAggregate() const {
        return aggregate;
    }

    static PersistentMapNode makeKv(K k, V &&v, A &&a) {
        return PersistentMapNode{
            .crit_bit = static_cast<uint8_t>(std::numeric_limits<K>::digits),
            .common_prefix = k,
            .aggregate = std::move(a),
            .content = Leaf{std::move(v)},
        };
    }

    // Drop n bits from the common prefix
    PersistentMapNode dropCommonPrefix(uint8_t n_bits) const {
        uint8_t rem_crit_bit = crit_bit - n_bits;
        return PersistentMapNode {
            rem_crit_bit,
            common_prefix >> n_bits,
            aggregate,
            content,
        };
    }

    // Precede the common prefix with a path from a parent
    void parentify(uint8_t par_crit_bit, K par_common_prefix, uint8_t split_bit) {
        crit_bit += par_crit_bit + 1;
        common_prefix = (((common_prefix << 1) | split_bit) << par_crit_bit) | par_common_prefix;
    }

    // Apply override on top of this
    template<class UF>
    static std::shared_ptr<PersistentMapNode> merge(
        const PersistentMapNode &base,
        const PersistentMapNode &override,
        const UF &upd_fns)
    {
        uint8_t new_crit_bit = std::min({
            static_cast<uint8_t>(std::countr_zero(base.common_prefix ^ override.common_prefix)),
            base.crit_bit,
            override.crit_bit,
        });
        // Keep prefix bits up to (but excluding) new_crit_bit
        K mask = new_crit_bit == std::numeric_limits<K>::digits
            ?  ~K{0} : ((K{1} << new_crit_bit) - K{1});
        K new_common_prefix = base.common_prefix & mask;
        bool full_common_prefix = new_crit_bit == base.crit_bit && new_crit_bit == override.crit_bit;
        if (full_common_prefix) {
            if (const auto *b_leaf = std::get_if<Leaf>(&base.content)) {
                const auto *o_leaf = std::get_if<Leaf>(&override.content);
                assert(o_leaf);
                std::optional<V> v = upd_fns.modify_fn(b_leaf->value, o_leaf->value);
                if (!v)
                    return nullptr;
                A agg = upd_fns.agg_init_fn(*v);
                return std::make_shared<PersistentMapNode>(
                    new_crit_bit,
                    new_common_prefix,
                    std::move(agg),
                    Leaf{std::move(*v)}
                );
            }
            // Case of two inner nodes is handled below to share parentification logic
        }

        // Result must be an inner node
        Children children;

        if (full_common_prefix) {
            const auto *b_inner = std::get_if<Inner>(&base.content);
            const auto *o_inner = std::get_if<Inner>(&override.content);
            for (uint8_t i : {0, 1})
                children[i] = merge(*(b_inner->children[i].get()), *(o_inner->children[i].get()), upd_fns);
            if (!children[0] && !children[1])
                return nullptr;
            for (uint8_t i : {0, 1}) {
                if (children[i] && !children[1 - i]) {
                    // Parentify this child. Admissible because we know that it was just produced by merge.
                    children[i].get()->parentify(new_crit_bit, new_common_prefix, i);
                    return children[i];
                }
            }
        } else if (new_crit_bit < base.crit_bit && new_crit_bit < override.crit_bit) {
            // Split at first differing bit
            children[(base.common_prefix >> new_crit_bit) & 1] = std::make_shared<PersistentMapNode>(base.dropCommonPrefix(new_crit_bit + 1));
            children[(override.common_prefix >> new_crit_bit) & 1] = std::make_shared<PersistentMapNode>(override.dropCommonPrefix(new_crit_bit + 1));
        } else if (new_crit_bit == base.crit_bit) {
            assert(new_crit_bit < override.crit_bit);
            // Split off base from longer override prefix
            const auto *b_inner = std::get_if<Inner>(&base.content);
            assert(b_inner);
            uint8_t o_bit = (override.common_prefix >> new_crit_bit) & 1;
            children[o_bit] = merge(*(b_inner->children[o_bit]), override.dropCommonPrefix(new_crit_bit + 1), upd_fns);
            const auto &other_child = b_inner->children[1 - o_bit];
            if (!children[o_bit]) {
                // Parentify only child
                auto res = std::make_shared<PersistentMapNode>(*other_child.get());
                res->parentify(new_crit_bit, new_common_prefix, 1 - o_bit);
                return res;
            } else {
                children[1 - o_bit] = other_child;
            }
        } else if (new_crit_bit == override.crit_bit) {
            assert(new_crit_bit < base.crit_bit);
            // Split off override from longer base prefix; same as before but swapped
            const auto *o_inner = std::get_if<Inner>(&override.content);
            assert(o_inner);
            uint8_t b_bit = (base.common_prefix >> new_crit_bit) & 1;
            children[b_bit] = merge(base.dropCommonPrefix(new_crit_bit + 1), *(o_inner->children[b_bit]), upd_fns);
            const auto &other_child = o_inner->children[1 - b_bit];
            if (!children[b_bit]) {
                // Parentify only child
                auto res = std::make_shared<PersistentMapNode>(*other_child.get());
                res->parentify(new_crit_bit, new_common_prefix, 1 - b_bit);
                return res;
            } else {
                children[1 - b_bit] = other_child;
            }
        }

        A joined = upd_fns.agg_join_fn(children[0]->aggregate, children[1]->aggregate);
        return std::make_shared<PersistentMapNode>(
            new_crit_bit,
            new_common_prefix,
            std::move(joined),
            Inner{std::move(children)}
        );
    }

    template<class F>
    void visit(const F &f, K prefix, uint8_t prefix_len) const {
        prefix |= (common_prefix << prefix_len);
        prefix_len += crit_bit;
        if (const auto *leaf = std::get_if<Leaf>(&content)) {
            f(prefix, leaf->value);
            return;
        }
        const auto &inner = *std::get_if<Inner>(&content);
        for (K i : {0, 1})
            inner.children[i]->visit(f, prefix | (i << prefix_len), prefix_len + 1);
    }
};

template<class K, class V, class A>
class PersistentMap {
    using Node = PersistentMapNode<K, V, A>;
    std::shared_ptr<Node> root;

    explicit PersistentMap(std::shared_ptr<Node> &&root) : root(std::move(root)) {}

public:

    PersistentMap() = default;

    static PersistentMap singleton(K k, V &&v, A &&a) {
        return PersistentMap(std::make_shared<Node>(Node::makeKv(k, std::move(v), std::move(a))));
    }

    bool empty() const {
        return !root;
    }

    const A *getAggregate() const {
        return empty() ? nullptr : &(root->getAggregate());
    }

    template<class UF>
    static PersistentMap merge(
        const PersistentMap &base,
        const PersistentMap &override,
        const UF &upd_fns)
    {
        if (base.empty())
            return override;
        if (override.empty())
            return base;
        auto root = Node::merge(*(base.root.get()), *(override.root.get()), upd_fns);
        return PersistentMap(std::move(root));
    }

    template<class F>
    void visit(const F &f) const {
        if (!empty())
            root->visit(f, K{0}, 0);
    }
};


// Copy-on-write data structure for a (partial) solution.
// Allows efficiently querying the number of serviced buildings, plus total coverage.
// Very cheap to copy and query, moderately cheap to update
// (but updates are not as cheap as in a mutable data structure with efficient undo / peek).
struct PersistentAntennaSet {
private:

    static auto getUpdateFns(double min_cov) {
        return UpdateFuncs {
            .modify_fn = [](const std::vector<Interval> &intervals1, const std::vector<Interval> &intervals2) -> std::vector<Interval> {
                std::vector<Interval> res;
                unionIntervals(intervals1, intervals2, res);
                return res;
            },
            .agg_init_fn = [min_cov](const std::vector<Interval> &intervals) {
                double cov = computeUnionLength(intervals);
                return Coverage{cov >= min_cov, cov, min_cov};
            },
            .agg_join_fn = [](const Coverage &cov1, const Coverage &cov2) {
                return cov1.merge(cov2);
            },
        };
    }

public:

    using BuildingId = unsigned;
    using AntennaId = unsigned;
    using BuildingMap = PersistentMap<BuildingId, std::vector<Interval>, Coverage>;
    using AntennaSet = PersistentMap<AntennaId, std::monostate, std::monostate>;
    BuildingMap building_coverages;
    AntennaSet antenna_set;

    const Coverage &getCoverage() const {
        static constexpr Coverage ZERO_COV;
        return building_coverages.empty() ? ZERO_COV : *building_coverages.getAggregate();
    }

    // TODO maybe also introduce more efficient multi-constructor?
    static PersistentAntennaSet singleton(const Antenna &antenna, double min_cov) {
        const auto update_fns = getUpdateFns(min_cov);
        BuildingMap building_coverages;
        for (const auto &cb : antenna.covered) {
            double cov = computeUnionLength(cb.intervals);
            Coverage coverage{
                .n_covered = cov >= min_cov,
                .total = cov,
                .min = min_cov,
            };
            building_coverages = BuildingMap::merge(
                building_coverages,
                BuildingMap::singleton(cb.id, std::vector(cb.intervals), std::move(coverage)),
                update_fns
            );
        }
        auto antenna_set = AntennaSet::singleton(antenna.id, std::monostate(), std::monostate());
        return PersistentAntennaSet{
            building_coverages,
            antenna_set,
        };
    }

    std::vector<AntennaId> getAntennaIds() const {
        std::vector<AntennaId> res;
        antenna_set.visit([&](AntennaId id, std::monostate) {
            res.emplace_back(id);
        });
        // Traversal gives sorted lexicographically *from LSB to MSB*.
        // Alternatively, could bitswap antenna IDs before insertion.
        // Probably doesn't really matter so just pay for a sort.
        std::sort(res.begin(), res.end());
        return res;
    }

    PersistentAntennaSet add(const PersistentAntennaSet &other, double min_cov) const {
        auto new_building_coverages = BuildingMap::merge(building_coverages, other.building_coverages, getUpdateFns(min_cov));
        auto new_antenna_set = AntennaSet::merge(antenna_set, other.antenna_set, UpdateFuncs {
            .modify_fn = [](std::monostate, std::monostate) { return std::make_optional(std::monostate()); },
            .agg_init_fn = [](std::monostate) { return std::monostate(); },
            .agg_join_fn = [](std::monostate, std::monostate) { return std::monostate(); },
        });
        return PersistentAntennaSet{
            new_building_coverages,
            new_antenna_set,
        };
    }

    PersistentAntennaSet remove(const PersistentAntennaSet &other, double min_cov) const {
        auto new_building_coverages = BuildingMap::merge(building_coverages, other.building_coverages, UpdateFuncs {
            .modify_fn = [](const std::vector<Interval> &intervals1, const std::vector<Interval> &intervals2) -> std::optional<std::vector<Interval>> {
                std::vector<Interval> res;
                removeIntervals(intervals1, intervals2, res);
                if (res.empty())
                    return std::nullopt;
                return std::make_optional(std::move(res));
            },
            .agg_init_fn = [min_cov](const std::vector<Interval> &intervals) {
                double cov = computeUnionLength(intervals);
                return Coverage{cov >= min_cov, cov, min_cov};
            },
            .agg_join_fn = [](const Coverage &cov1, const Coverage &cov2) {
                return cov1.merge(cov2);
            },
        });
        auto new_antenna_set = AntennaSet::merge(antenna_set, other.antenna_set, UpdateFuncs {
            .modify_fn = [](std::monostate, std::monostate) { return std::nullopt; },
            .agg_init_fn = [](std::monostate) { return std::monostate(); },
            .agg_join_fn = [](std::monostate, std::monostate) { return std::monostate(); },
        });
        return PersistentAntennaSet{
            new_building_coverages,
            new_antenna_set,
        };
    }
};