// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef LEXER_EXTRAS_H
#define LEXER_EXTRAS_H

// v0.1.55 — bulk lexer additions targeting 80+ supported languages.
//
// Each class below is a small QsciLexer* subclass (mostly QsciLexerCPP /
// Python / Ruby / Java / HTML / Bash / Properties) overriding only
// language() and keywords() to give Notepatra correct syntax highlighting
// for languages QScintilla doesn't ship native lexers for.
//
// Why subclass instead of writing a full lexer? Most modern languages
// share enough surface grammar with C, Python, Ruby, or Java for the
// existing Scintilla lexer to handle 95% of tokenisation correctly —
// strings, numbers, comments, braces, operators all light up. The only
// difference is which words are keywords vs identifiers, which is
// exactly what `keywords()` overrides.
//
// Languages that need genuinely different lexing (Lisp / Smalltalk /
// Erlang / Haskell / OCaml — heavy s-expr or whitespace-significant
// syntax) are NOT in this file; they'd need QsciLexerCustom subclasses
// with token-by-token state machines.

#include <Qsci/qscilexercpp.h>
#include <Qsci/qscilexerpython.h>
#include <Qsci/qscilexerruby.h>
#include <Qsci/qscilexerjava.h>
#include <Qsci/qscilexerhtml.h>
#include <Qsci/qscilexerbash.h>
#include <Qsci/qscilexerproperties.h>
#include <Qsci/qscilexerjson.h>

// Macro: declare a "subclass + override keywords()" lexer in one line.
// Implementation file fills out keywords() per language.
#define DECL_LEXER(Name, Base, Tag) \
    class Name : public Base { \
    public: \
        explicit Name(QObject *parent = nullptr) : Base(parent) {} \
        const char *language() const override { return Tag; } \
        const char *keywords(int set) const override; \
    };

// ─── C-family lexers (QsciLexerCPP base) ─────────────────────────────────
DECL_LEXER(LexerDart,        QsciLexerCPP, "Dart")
DECL_LEXER(LexerSolidity,    QsciLexerCPP, "Solidity")
DECL_LEXER(LexerZig,         QsciLexerCPP, "Zig")
DECL_LEXER(LexerVala,        QsciLexerCPP, "Vala")
DECL_LEXER(LexerHack,        QsciLexerCPP, "Hack")
DECL_LEXER(LexerJulia,       QsciLexerCPP, "Julia")
DECL_LEXER(LexerR,           QsciLexerCPP, "R")
DECL_LEXER(LexerProtobuf,    QsciLexerCPP, "Protobuf")
DECL_LEXER(LexerFSharp,      QsciLexerCPP, "FSharp")
DECL_LEXER(LexerHCL,         QsciLexerCPP, "HCL")
DECL_LEXER(LexerThrift,      QsciLexerCPP, "Thrift")
DECL_LEXER(LexerGraphQL,     QsciLexerCPP, "GraphQL")

// ─── Python-family lexers ─────────────────────────────────────────────────
DECL_LEXER(LexerGDScript,    QsciLexerPython, "GDScript")
DECL_LEXER(LexerNim,         QsciLexerPython, "Nim")
DECL_LEXER(LexerCython,      QsciLexerPython, "Cython")
DECL_LEXER(LexerMojo,        QsciLexerPython, "Mojo")

// ─── Ruby-family lexers ───────────────────────────────────────────────────
DECL_LEXER(LexerCrystal,     QsciLexerRuby, "Crystal")
DECL_LEXER(LexerElixir,      QsciLexerRuby, "Elixir")

// ─── Java/JVM-family lexers ───────────────────────────────────────────────
DECL_LEXER(LexerScala,       QsciLexerJava, "Scala")
DECL_LEXER(LexerGroovy,      QsciLexerJava, "Groovy")
DECL_LEXER(LexerApex,        QsciLexerJava, "Apex")

// ─── HTML-family templating engines ───────────────────────────────────────
DECL_LEXER(LexerJinja,       QsciLexerHTML, "Jinja")
DECL_LEXER(LexerLiquid,      QsciLexerHTML, "Liquid")
DECL_LEXER(LexerTwig,        QsciLexerHTML, "Twig")

// ─── Shell-family lexers ──────────────────────────────────────────────────
DECL_LEXER(LexerDockerfile,  QsciLexerBash, "Dockerfile")
DECL_LEXER(LexerFish,        QsciLexerBash, "Fish")
DECL_LEXER(LexerNushell,     QsciLexerBash, "Nushell")

// ─── Properties / config-file lexers ──────────────────────────────────────
DECL_LEXER(LexerToml,        QsciLexerProperties, "TOML")
DECL_LEXER(LexerEnv,         QsciLexerProperties, "DotEnv")
DECL_LEXER(LexerGitignore,   QsciLexerProperties, "Gitignore")

// ─── JSON-family ──────────────────────────────────────────────────────────
DECL_LEXER(LexerJson5,       QsciLexerJSON, "JSON5")

// ─── Reference / bibliography ─────────────────────────────────────────────
DECL_LEXER(LexerBibTeX,      QsciLexerCPP, "BibTeX")

#undef DECL_LEXER

#endif
