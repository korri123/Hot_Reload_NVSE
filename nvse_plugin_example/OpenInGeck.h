#pragma once

const UInt16 g_geckPort = 12058;

enum class TransferType
{
	kNull,
	kOpenRef
};

class GeckTransferObject
{
public:
	TransferType type{};


	GeckTransferObject() = default;

	explicit GeckTransferObject(TransferType type)
		: type(type)
	{
	}
};

class GeckOpenRefTransferObject
{
public:
	UInt32 refId{};

	GeckOpenRefTransferObject() = default;

	explicit GeckOpenRefTransferObject(UInt32 refId)
		: refId(refId)
	{
	}
};

#if EDITOR

void StartGeckServer();

#endif