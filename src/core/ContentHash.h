#pragma once

#include <string>

// FNV-1a, 64-bit, as 16 lowercase hex digits. Identical content always gets the
// same hash, so each version of a file is stored once and tampering shows up as
// a mismatch. It is fast and simple, not cryptographic: it detects accidents and
// casual edits, not a deliberate attacker.
std::string contentHash(const std::string& content);

// True for 16 lowercase hex digits, the form contentHash returns.
bool isContentHash(const std::string& text);
