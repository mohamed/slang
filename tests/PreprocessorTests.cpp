#include "catch.hpp"
#include "slang.h"

using namespace slang;

namespace {

BumpAllocator alloc;
Diagnostics diagnostics;

SourceTracker& getTracker() {
    static SourceTracker* tracker = nullptr;
    if (!tracker) {
        tracker = new SourceTracker();
        tracker->addUserDirectory("../../../tests/data/");
    }
    return *tracker;
}

const Token& lexToken(const SourceText& text) {
    diagnostics.clear();
    Preprocessor preprocessor(getTracker(), alloc, diagnostics);
    Lexer lexer(FileID(), text, preprocessor);

    Token* token = lexer.lex();
    REQUIRE(token != nullptr);
    return *token;
}

TEST_CASE("Include File", "[preprocessor]") {
    auto& text = "`include \"include.svh\"";
    auto& token = lexToken(text);

    CHECK(token.kind == TokenKind::StringLiteral);

    // there should be one error about a non-existent include file
    REQUIRE(!diagnostics.empty());
    CHECK(diagnostics.last().code == DiagCode::CouldNotOpenIncludeFile);
}

void testDirective(TriviaKind kind) {
    auto& text = getTriviaKindText(kind);

    diagnostics.clear();
    Preprocessor preprocessor(getTracker(), alloc, diagnostics);
    Lexer lexer(FileID(), SourceText::fromNullTerminated(text), preprocessor);

    Token* token = lexer.lexDirectiveMode();
    REQUIRE(token != nullptr);

    CHECK(token->kind == TokenKind::Directive);
    CHECK(token->toFullString() == text);
    CHECK(token->valueText() == text);
    CHECK(diagnostics.empty());
}

TEST_CASE("Directives", "[preprocessor]") {
    testDirective(TriviaKind::BeginKeywordsDirective);
    testDirective(TriviaKind::CellDefineDirective);
    testDirective(TriviaKind::DefaultNetTypeDirective);
    testDirective(TriviaKind::DefineDirective);
    testDirective(TriviaKind::ElseDirective);
    testDirective(TriviaKind::ElseIfDirective);
    testDirective(TriviaKind::EndKeywordsDirective);
    testDirective(TriviaKind::EndCellDefineDirective);
    testDirective(TriviaKind::EndIfDirective);
    testDirective(TriviaKind::IfDefDirective);
    testDirective(TriviaKind::IfNDefDirective);
    testDirective(TriviaKind::IncludeDirective);
    testDirective(TriviaKind::LineDirective);
    testDirective(TriviaKind::NoUnconnectedDriveDirective);
    testDirective(TriviaKind::PragmaDirective);
    testDirective(TriviaKind::ResetAllDirective);
    testDirective(TriviaKind::TimescaleDirective);
    testDirective(TriviaKind::UnconnectedDriveDirective);
    testDirective(TriviaKind::UndefDirective);
    testDirective(TriviaKind::UndefineAllDirective);
}

TEST_CASE("Macro define (simple)", "[preprocessor]") {
    auto& text = "`define FOO (1)";
    auto& token = lexToken(text);

    CHECK(token.kind == TokenKind::EndOfFile);
    CHECK(token.toFullString() == text);
    CHECK(diagnostics.empty());
    REQUIRE(token.trivia.count() == 1);
    REQUIRE(token.trivia[0]->kind == TriviaKind::DefineDirective);

    DefineDirectiveTrivia* def = (DefineDirectiveTrivia*)token.trivia[0];
    CHECK(def->name->valueText() == "FOO");
    CHECK(def->endOfDirective);
    CHECK(def->directive);
    CHECK(!def->formalArguments);
    REQUIRE(def->body.count() == 3);
    CHECK(def->body[1]->kind == TokenKind::IntegerLiteral);
}

TEST_CASE("Macro define (function-like)", "[preprocessor]") {
    auto& text = "`define FOO(a) a+1";
    auto& token = lexToken(text);

    CHECK(token.kind == TokenKind::EndOfFile);
    CHECK(token.toFullString() == text);
    CHECK(diagnostics.empty());
    REQUIRE(token.trivia.count() == 1);
    REQUIRE(token.trivia[0]->kind == TriviaKind::DefineDirective);

    DefineDirectiveTrivia* def = (DefineDirectiveTrivia*)token.trivia[0];
    CHECK(def->name->valueText() == "FOO");
    CHECK(def->endOfDirective);
    CHECK(def->directive);
    CHECK(def->formalArguments);
    REQUIRE(def->body.count() == 3);
    CHECK(def->body[2]->kind == TokenKind::IntegerLiteral);
}

TEST_CASE("Macro usage (undefined)", "[preprocessor]") {
    auto& text = "`FOO";
    auto& token = lexToken(text);

    REQUIRE(!diagnostics.empty());
    CHECK(diagnostics.last().code == DiagCode::UnknownDirective);
}

TEST_CASE("Macro usage (simple)", "[preprocessor]") {
    auto& text = "`define FOO 42\n`FOO";
    auto& token = lexToken(text);

    CHECK(token.kind == TokenKind::IntegerLiteral);
    CHECK(token.numericValue().integer == 42);
    CHECK(diagnostics.empty());
}

}