#pragma once

#include "PromptTypes.h"
#include "UnitPack.h"

class IbReference;

class UnitContextBuilder
{
public:
    static UnitContext none();
    static UnitContext fromPack(const UnitPack &pack, int topicIndex, const IbReference &ib);
};
