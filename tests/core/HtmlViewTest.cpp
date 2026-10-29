#include "TestHarness.h"

#include <string>

#include "TempDir.h"
#include "core/HtmlView.h"
#include "core/Repository.h"
#include "core/WorkingTree.h"

TEST(theViewOfAnEmptyRepositorySaysHowToStart) {
    TempDir dir;
    const Repository repo = Repository::init(dir.path());
    const std::string page = HtmlView::render(repo);
    CHECK(page.rfind("<!doctype html>", 0) == 0);
    CHECK(page.find("No commits yet") != std::string::npos);
    CHECK(page.find("All clear") != std::string::npos);
}

TEST(theViewShowsCommitsSheetsAndEscapesText) {
    TempDir dir;
    Repository repo = Repository::init(dir.path());
    WorkingTree tree(dir.path());
    tree.write("budget.csv", "Rent,10,=B1*2\n");
    repo.track("budget.csv");
    repo.commit("first <draft>", "Tanishka");

    const std::string page = HtmlView::render(repo);
    CHECK(page.find("first &lt;draft&gt;") != std::string::npos);
    CHECK(page.find("first <draft>") == std::string::npos);
    CHECK(page.find("budget.csv") != std::string::npos);
    CHECK(page.find(">20</td>") != std::string::npos);  // the formula, worked out
    CHECK(page.find("1</b> commit<") != std::string::npos);
}

// The finale of the demo, as the page shows it.
TEST(theViewLightsUpAParadoxFromAMerge) {
    TempDir dir;
    Repository repo = Repository::init(dir.path());
    WorkingTree tree(dir.path());
    tree.write("sheet.csv", "Rent,10,=B1*2\n");
    repo.track("sheet.csv");
    repo.commit("base", "Tanishka");
    repo.createBranch("experiment");
    tree.write("sheet.csv", "Rent,50,=B1*2\n");
    repo.commit("rent is 50", "Tanishka");
    repo.switchBranch("experiment");
    tree.write("sheet.csv", "Rent,10,\"=IF(B1>20,\"\"OVER BUDGET\"\",B1*2)\"\n");
    repo.commit("warn when over 20", "Tanishka");
    repo.switchBranch("main");
    repo.merge("experiment", "Tanishka");
    repo.commit("checked it", "Tanishka");

    const std::string page = HtmlView::render(repo);
    CHECK(page.find("Paradox detected in sheet.csv, cell C1") != std::string::npos);
    CHECK(page.find("merged reality</small>OVER BUDGET") != std::string::npos);
    CHECK(page.find("class=\"f hot\"") != std::string::npos);
}
