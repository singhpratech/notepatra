// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NOTEPATRA_AGENT_REPEAT_GUARD_H
#define NOTEPATRA_AGENT_REPEAT_GUARD_H

// ═══════════════════════════════════════════════════════════════════════
// AgentRepeatGuard — agent-loop perseveration breaker (pure logic).
//
// Small models stuck on a failing tool call tend to re-send the SAME
// call verbatim until the round budget burns out, then return empty
// output. This class is the deterministic detector the agent loop
// (AIPanel::handleToolCall) consults:
//
//   • after every executed call, recordFailure()/recordSuccess() track
//     the streak of CONSECUTIVE byte-identical failing calls (same tool
//     name + same canonical args JSON). Any success or any different
//     call resets the streak.
//   • when the 2nd identical failure lands, recordFailure() returns
//     NudgeOnce exactly once — the loop injects a one-time system note
//     telling the model to change strategy.
//   • before executing a call, shouldRefuse() answers true once the
//     same signature has already failed twice — the loop then refuses
//     to execute it and returns error_kind:"repeated_call" instead.
//
// Also hosts forcedFinalText(), the force-finalize formatter: when a
// tool-active turn ends with EMPTY model output, the loop surfaces the
// last tool error (or a summary of the actions taken) instead of a
// silent empty reply.
//
// Header-only and model-agnostic by construction: no model names, no
// content heuristics — only byte-equality of (tool, args) and error/ok
// outcomes. Unit-tested in test_ai_tools.cpp.
// ═══════════════════════════════════════════════════════════════════════

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>

class AgentRepeatGuard {
public:
    // Canonical signature for "byte-identical call": tool name + compact
    // JSON of the args object. QJsonObject serialises with sorted keys,
    // so two args objects with equal content always produce equal bytes.
    static QString signature(const QString &toolName, const QJsonObject &args) {
        return toolName + QLatin1Char('\n') +
               QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact));
    }

    // True once this exact signature has already failed 2+ times in a row —
    // the caller must NOT execute the call again.
    bool shouldRefuse(const QString &sig) const {
        return m_consecutiveFailures >= 2 && sig == m_lastFailSig;
    }

    enum class Note { None, NudgeOnce };

    // Record a failing call. Returns NudgeOnce exactly when the identical
    // failure count reaches 2 (once per streak) so the caller can inject
    // a single change-strategy system note.
    Note recordFailure(const QString &sig) {
        if (sig == m_lastFailSig) {
            ++m_consecutiveFailures;
        } else {
            m_lastFailSig = sig;
            m_consecutiveFailures = 1;
            m_nudgeSent = false;
        }
        if (m_consecutiveFailures == 2 && !m_nudgeSent) {
            m_nudgeSent = true;
            return Note::NudgeOnce;
        }
        return Note::None;
    }

    // Any successful call breaks the consecutive-failure streak.
    void recordSuccess() { reset(); }

    // New user turn / Stop / clear — drop all streak state.
    void reset() {
        m_lastFailSig.clear();
        m_consecutiveFailures = 0;
        m_nudgeSent = false;
    }

    int consecutiveFailures() const { return m_consecutiveFailures; }

    // Force-finalize formatter — the assistant text shown when a turn with
    // tool activity ends with empty model output. Prefers the last tool
    // error; falls back to the action log; returns empty only when there
    // is nothing at all to report (caller keeps legacy behaviour then).
    static QString forcedFinalText(const QString &lastToolError,
                                   const QStringList &actions) {
        if (lastToolError.isEmpty() && actions.isEmpty()) return QString();
        QString out = QStringLiteral(
            "The run ended without a final answer from the model.");
        if (!lastToolError.isEmpty()) {
            out += QStringLiteral("\n\nLast tool error: ") + lastToolError;
        }
        if (!actions.isEmpty()) {
            out += QStringLiteral("\n\nActions taken this turn:");
            const int maxShown = 12;
            for (int i = 0; i < actions.size(); ++i) {
                if (i == maxShown) {
                    out += QStringLiteral("\n- … and %1 more")
                               .arg(actions.size() - maxShown);
                    break;
                }
                out += QStringLiteral("\n- ") + actions.at(i);
            }
        }
        return out;
    }

private:
    QString m_lastFailSig;
    int     m_consecutiveFailures = 0;
    bool    m_nudgeSent = false;
};

#endif // NOTEPATRA_AGENT_REPEAT_GUARD_H
