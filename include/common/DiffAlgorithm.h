#pragma once

#include "common/DiffResult.h"
#include "common/Snapshot.h"

class DiffAlgorithm {
public:
    virtual DiffResult diff(const Snapshot& a, const Snapshot& b) const = 0;
    virtual ~DiffAlgorithm() = default;
};
