#pragma once

#include <unordered_map>
#include <utility>

#include <boost/dynamic_bitset.hpp>

namespace algos {

class SearchTreeEulerFD {
public:
    using Bitset = boost::dynamic_bitset<>;
    using BitsetConsumer = std::function<void(Bitset const&)>;

private:
    struct Node {
        size_t bit_{};

        Bitset set_;    // Set of attributes which corresponds this node
        Bitset union_;  // Union of children of this node
        Bitset inter_;  // Intersection of children of this node

        std::shared_ptr<Node> left_{};
        std::shared_ptr<Node> right_{};
        std::weak_ptr<Node> parent_{};

        [[nodiscard]] bool IsLeaf() const;

        [[nodiscard]] Bitset const& GetUnion() const;
        [[nodiscard]] Bitset const& GetInter() const;

        // For inner nodes
        Node(size_t bit, Bitset sets_union, Bitset sets_inter, std::shared_ptr<Node> const& parent,
             std::shared_ptr<Node> left = nullptr, std::shared_ptr<Node> right = nullptr);

        // For leaves
        Node(size_t bit, Bitset set, std::shared_ptr<Node> const& parent);

        // For both types of nodes
        Node(size_t bit, Bitset set, Bitset sets_union, Bitset sets_inter,
             std::shared_ptr<Node> const& parent, std::shared_ptr<Node> left = nullptr,
             std::shared_ptr<Node> right = nullptr);
    };

    size_t cardinality_{};
    size_t number_of_attributes_{};
    std::shared_ptr<Node> root_{};

    void CreateSingleElementSets(Bitset const& set);

    void CollectSubsets(Bitset const& set, std::shared_ptr<Node> const& current_node,
                        BitsetConsumer const& collect, bool& go_further) const;
    [[nodiscard]] bool SupersetsTraverse(Bitset const& set,
                                         std::shared_ptr<Node> const& current_node) const;

    void ForEach(std::shared_ptr<Node> const& current_node, BitsetConsumer const& collect) const;

    [[nodiscard]] std::shared_ptr<Node> FindNode(Bitset const& set);
    void CutLeaf(std::shared_ptr<Node> const& node_to_remove);

    static void InsertLeafIntoEnd(std::shared_ptr<Node> const& current_node, Bitset const& set,
                                  size_t node_bit, size_t set_bit);
    static void InsertLeafIntoMiddle(std::shared_ptr<Node> const& current_node, Bitset const& set,
                                     size_t set_bit);

    static void UpdateInterAndUnion(std::shared_ptr<Node> const& node);

    [[nodiscard]] static std::pair<size_t, size_t> FindNodeAndSetBits(Bitset const& node_set,
                                                                      Bitset const& set);

public:
    explicit SearchTreeEulerFD(size_t number_of_attributes);
    explicit SearchTreeEulerFD(Bitset const& set);

    [[nodiscard]] size_t GetCardinality() const {
        return cardinality_;
    }

    bool Add(Bitset const& set);
    bool Remove(Bitset const& set);

    void ForEach(BitsetConsumer const& collect) const;
    void ForEachSubset(Bitset const& set, BitsetConsumer const& collect) const;
    [[nodiscard]] bool ContainsAnySubsetOf(Bitset const& set) const;
    [[nodiscard]] bool ContainsAnySupersetOf(Bitset const& set) const;
};

}  // namespace algos
