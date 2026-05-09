#ifndef CREDSCRUB_H
#define CREDSCRUB_H

#include <QString>

// Credential scrubber. Removes high-confidence secret patterns from any chunk
// of text derived from the user's editor before that text is forwarded to a
// remote AI backend.
//
// Why this exists: in v0.1.55-prerelease, a user pasted their OpenRouter key
// into an editor buffer to test it, then asked the AI Assistant a casual
// question. The "current file as context" feature happily forwarded the key
// to OpenRouter as part of the prompt, where it landed in their server-side
// logs. This module is the surgical fix for that class of leak — any code
// path that sends editor-sourced text to a remote model MUST run it through
// CredScrub::redact() first.
//
// Patterns are deliberately tight (high precision) so we don't redact code
// that merely *looks* tokenish (UUIDs, git SHAs, hex-encoded payloads). The
// trade-off favours false negatives over false positives: missing one obscure
// vendor's token format is recoverable; over-redacting normal code makes the
// AI useless. See test_credscrub.cpp for the canonical pattern catalogue.
namespace CredScrub {

// Replace every credential-shaped substring in `text` with a marker like
// "[REDACTED-OPENROUTER-KEY]". Returns the redacted text.
//
// If `redactedCount` is non-null, it receives the total number of substrings
// that were replaced — handy for status banners ("⚠ redacted 1 credential
// before sending"). 0 means the text was clean.
QString redact(const QString &text, int *redactedCount = nullptr);

}  // namespace CredScrub

#endif
