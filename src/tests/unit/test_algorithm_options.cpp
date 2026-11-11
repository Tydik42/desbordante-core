#include <gtest/gtest.h>

#include "core/algorithms/algorithm.h"
#include "core/config/exceptions.h"

namespace {

class NestedOptionsAlgorithm : public algos::Algorithm {
public:
    NestedOptionsAlgorithm() {
        RegisterOption(
                config::Option{&parent_, "parent", "Parent option", false}.SetConditionalOpts(
                        {{[](bool enabled) { return enabled; }, {"child", "sibling"}}}));
        RegisterOption(config::Option{&child_, "child", "Child option", false}.SetConditionalOpts(
                {{[](bool enabled) { return enabled; }, {"grandchild"}}}));
        RegisterOption(config::Option{&grandchild_, "grandchild", "Grandchild option", false});
        RegisterOption(config::Option{&sibling_, "sibling", "Sibling option", false});
        MakeOptionsAvailable({"parent"});
    }

private:
    bool parent_ = false;
    bool child_ = false;
    bool grandchild_ = false;
    bool sibling_ = false;

    void ResetState() final {}

    void LoadDataInternal() final {}

    void ExecuteInternal() final {}
};

void EnableAll(NestedOptionsAlgorithm& algorithm) {
    algorithm.SetOption("parent", true);
    algorithm.SetOption("child", true);
    algorithm.SetOption("grandchild", true);
    algorithm.SetOption("sibling", true);
}

}  // namespace

TEST(AlgorithmOptionsTest, UnsettingParentClearsEveryDescendant) {
    NestedOptionsAlgorithm algorithm;
    EnableAll(algorithm);
    algorithm.UnsetOption("parent");
    EXPECT_TRUE(algorithm.GetOptValues().empty());
    for (auto name : {"child", "grandchild", "sibling"}) {
        EXPECT_FALSE(algorithm.OptionIsSet(name));
        EXPECT_THROW(algorithm.SetOption(name, true), config::ConfigurationError);
    }
    algorithm.UnsetOption("parent");
    EnableAll(algorithm);
    EXPECT_EQ(algorithm.GetOptValues().size(), 4U);
}

TEST(AlgorithmOptionsTest, DisablingParentDoesNotRestoreStaleDescendants) {
    NestedOptionsAlgorithm algorithm;
    EnableAll(algorithm);
    algorithm.SetOption("parent", false);
    EXPECT_EQ(algorithm.GetOptValues().size(), 1U);
    EXPECT_FALSE(algorithm.OptionIsSet("child"));
    EXPECT_FALSE(algorithm.OptionIsSet("grandchild"));
    algorithm.SetOption("parent", true);
    EXPECT_FALSE(algorithm.OptionIsSet("child"));
    EXPECT_FALSE(algorithm.OptionIsSet("grandchild"));
    EXPECT_THROW(algorithm.SetOption("grandchild", true), config::ConfigurationError);
    algorithm.SetOption("child", true);
    EXPECT_FALSE(algorithm.OptionIsSet("grandchild"));
    algorithm.SetOption("grandchild", true);
    EXPECT_TRUE(algorithm.OptionIsSet("grandchild"));
}

TEST(AlgorithmOptionsTest, UnsettingChildPreservesParentAndSibling) {
    NestedOptionsAlgorithm algorithm;
    EnableAll(algorithm);
    algorithm.UnsetOption("child");
    EXPECT_TRUE(algorithm.OptionIsSet("parent"));
    EXPECT_TRUE(algorithm.OptionIsSet("sibling"));
    EXPECT_FALSE(algorithm.OptionIsSet("child"));
    EXPECT_FALSE(algorithm.OptionIsSet("grandchild"));
    EXPECT_EQ(algorithm.GetOptValues().size(), 2U);
    EXPECT_THROW(algorithm.SetOption("grandchild", true), config::ConfigurationError);
}
