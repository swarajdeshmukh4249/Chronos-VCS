#include "TestHarness.h"

#include <algorithm>
#include <string>
#include <vector>

#include "common/Exceptions.h"
#include "sheet/Cell.h"
#include "sheet/DependencyGraph.h"
#include "sheet/Grid.h"

namespace {

CellRef at(const char* name) {
    return CellRef::parse(name);
}

std::vector<CellRef> refs(std::initializer_list<const char*> names) {
    std::vector<CellRef> result;
    for (const char* name : names) result.push_back(at(name));
    return result;
}

std::size_t position(const std::vector<CellRef>& order, const char* name) {
    return static_cast<std::size_t>(std::find(order.begin(), order.end(), at(name)) - order.begin());
}

std::string cycleMessage(const DependencyGraph& graph) {
    try {
        graph.evaluationOrder();
    } catch (const CircularReferenceException& e) {
        return e.what();
    }
    return "";
}

}  // namespace

// Issue #28
TEST(listsWhatEachCellDependsOn) {
    Grid<Cell> grid;
    grid.set("A1", 10);
    grid.set("B1", 2);
    grid.set("C1", "=A1+B1*A1");
    grid.set("D1", "=SUM(A1:B2)");
    grid.set("E1", "=IF(C1>5, \"big\", D1)");
    DependencyGraph graph(grid);

    CHECK(graph.dependenciesOf(at("C1")) == refs({"A1", "B1"}));
    CHECK(graph.dependenciesOf(at("D1")) == refs({"A1", "B1", "A2", "B2"}));
    CHECK(graph.dependenciesOf(at("E1")) == refs({"C1", "D1"}));
    CHECK(graph.dependenciesOf(at("A1")).empty());
    CHECK(graph.hasFormula(at("C1")));
    CHECK(!graph.hasFormula(at("A1")));
}

TEST(listsWhoDependsOnEachCell) {
    Grid<Cell> grid;
    grid.set("A1", 10);
    grid.set("B1", "=A1*2");
    grid.set("C1", "=B1+A1");
    grid.set("D1", "=C1");
    grid.set("E1", 5);
    DependencyGraph graph(grid);

    CHECK(graph.dependentsOf(at("A1")) == refs({"B1", "C1"}));
    CHECK(graph.dependentsOf(at("D1")).empty());
    CHECK(graph.affectedBy(at("A1")) == std::set<CellRef>({at("B1"), at("C1"), at("D1")}));
    CHECK(graph.affectedBy(at("C1")) == std::set<CellRef>({at("D1")}));
    CHECK(graph.affectedBy(at("E1")).empty());
}

TEST(brokenFormulaHasNoDependencies) {
    Grid<Cell> grid;
    grid.set("A1", "=B1+");
    DependencyGraph graph(grid);
    CHECK(graph.hasFormula(at("A1")));
    CHECK(graph.dependenciesOf(at("A1")).empty());
}

TEST(evaluationOrderPutsDependenciesFirst) {
    Grid<Cell> grid;
    grid.set("A1", 1);
    grid.set("D1", "=B1+C1");
    grid.set("B1", "=A1+1");
    grid.set("C1", "=B1*2");
    grid.set("Z9", "=7");
    DependencyGraph graph(grid);

    const std::vector<CellRef> order = graph.evaluationOrder(at("D1"));
    CHECK(order == refs({"B1", "C1", "D1"}));

    const std::vector<CellRef> all = graph.evaluationOrder();
    CHECK(all.size() == 4);
    CHECK(position(all, "B1") < position(all, "C1"));
    CHECK(position(all, "C1") < position(all, "D1"));
}

TEST(evaluationOrderSkipsCellsAlreadyDone) {
    Grid<Cell> grid;
    grid.set("A1", "=1");
    grid.set("B1", "=A1+1");
    grid.set("C1", "=B1+1");
    DependencyGraph graph(grid);
    const auto bDone = [](const CellRef& ref) { return ref == CellRef::parse("B1"); };
    CHECK(graph.evaluationOrder(at("C1"), bDone) == refs({"C1"}));
}

// Issue #29
TEST(detectsCircularReferences) {
    Grid<Cell> grid;
    grid.set("A1", "=B1");
    grid.set("B1", "=A1");
    CHECK(cycleMessage(DependencyGraph(grid)) == "Circular reference: A1 -> B1 -> A1");

    Grid<Cell> self;
    self.set("C3", "=C3+1");
    CHECK(cycleMessage(DependencyGraph(self)) == "Circular reference: C3 -> C3");

    Grid<Cell> throughRange;
    throughRange.set("A1", "=SUM(A1:A3)");
    CHECK(cycleMessage(DependencyGraph(throughRange)) == "Circular reference: A1 -> A1");
}

TEST(cycleMessageStartsWhereTheLoopStarts) {
    Grid<Cell> grid;
    grid.set("A1", "=B1");
    grid.set("B1", "=C1");
    grid.set("C1", "=D1");
    grid.set("D1", "=B1");
    std::string message;
    try {
        DependencyGraph(grid).evaluationOrder(at("A1"));
    } catch (const CircularReferenceException& e) {
        message = e.what();
    }
    CHECK(message == "Circular reference: B1 -> C1 -> D1 -> B1");
}

TEST(diamondIsNotACycle) {
    Grid<Cell> grid;
    grid.set("A1", 1);
    grid.set("B1", "=A1");
    grid.set("C1", "=A1");
    grid.set("D1", "=B1+C1");
    CHECK(cycleMessage(DependencyGraph(grid)).empty());
}

TEST(longChainHasNoStackOverflow) {
    Grid<Cell> grid;
    grid.set("A1", 1);
    for (int row = 2; row <= 50000; ++row) {
        grid.set(CellRef{row - 1, 0}, "=A" + std::to_string(row - 1) + "+1");
    }
    grid.set("A1", "=A50000");
    CHECK(cycleMessage(DependencyGraph(grid)) ==
          "Circular reference: A1 -> A50000 -> A49999 -> ... -> A3 -> A2 -> A1 (50000 cells)");
}

TEST(sevenCellCycleIsShownInFull) {
    Grid<Cell> grid;
    for (int row = 1; row <= 7; ++row) {
        grid.set(CellRef{row - 1, 0}, "=A" + std::to_string(row % 7 + 1));
    }
    CHECK(cycleMessage(DependencyGraph(grid)) ==
          "Circular reference: A1 -> A2 -> A3 -> A4 -> A5 -> A6 -> A7 -> A1");
}
