#pragma once
#include <queue>
#include <functional>
#include "common/ICriticalSection.h"

extern std::queue<std::function<void()>> g_mainThreadExecutionQueue;
extern ICriticalSection g_criticalSection;

class ScriptTransferObject
{
public:
	UInt32 scriptRefID;
	UInt32 nameLength;
	UInt32 dataLength;
	UInt32 numVars;
	UInt32 numRefs;
	UInt32 type;
};

class VarInfoTransferObject
{
public:
	UInt32 idx{};
	UInt32 type{};
	UInt32 nameLength{};
	
	VarInfoTransferObject() = default;

	VarInfoTransferObject(const UInt32 idx, const UInt32 type, const UInt32 nameLength) : idx(idx), type(type), nameLength(nameLength) {}
};

class RefInfoTransferObject
{
public:
	UInt32 nameLength{};
	UInt32 formId{};
	UInt32 esmNameLength{};
	UInt32 varIdx{};

	RefInfoTransferObject(const UInt32 nameLength, const UInt32 formId, const UInt32 esmNameLength, const UInt32 varIdx)
		: nameLength(nameLength), formId(formId), esmNameLength(esmNameLength), varIdx(varIdx)
	{}

	RefInfoTransferObject() = default;

};

const auto g_nvsePort = 12059;

void InitializeHotReloadRuntime();
void InitializeHotReloadEditor();