// generated on Sep 17, 2010 4:11 PM

#ifndef __YATESNMP_H
#define __YATESNMP_H

#include <yateasn.h>

#ifdef _WINDOWS

#ifdef LIBYSNMP_EXPORTS
#define YSNMP_API __declspec(dllexport)
#else
#ifndef LIBYSNMP_STATIC
#define YSNMP_API __declspec(dllimport)
#endif
#endif

#endif /* _WINDOWS */

#ifndef YSNMP_API
#define YSNMP_API
#endif

using namespace TelEngine;

namespace Snmp {

class ObjectName;
class ObjectSyntax;
class SimpleSyntax;
class ApplicationSyntax;
class IpAddress;
class Counter32;
class Unsigned32;
class Gauge32;
class TimeTicks;
class Opaque;
class Counter64;
class PDUs;
class GetRequest_PDU;
class GetNextRequest_PDU;
class Response_PDU;
class SetRequest_PDU;
class GetBulkRequest_PDU;
class InformRequest_PDU;
class SNMPv2_Trap_PDU;
class Report_PDU;
class PDU;
class BulkPDU;
class VarBind;
class VarBindList;

class DisplayString;
class PhysAddress;
class MacAddress;
class TruthValue;
class TestAndIncr;
class AutonomousType;
class InstancePointer;
class VariablePointer;
class RowPointer;
class RowStatus;
class TimeStamp;
class TimeInterval;
class DateAndTime;
class StorageType;
class TDomain;
class TAddress;

class SNMPv3Message;
class HeaderData;
class ScopedPduData;
class ScopedPDU;


class Message;

class KeyChange;
class UsmUserEntry;

class SnmpEngineID;
class SnmpSecurityModel;
class SnmpMessageProcessingModel;
class SnmpSecurityLevel;
class SnmpAdminString;

class UsmSecurityParameters;

class SysOREntry;

// defined in SNMPv2-PDU
class YSNMP_API ObjectName : public AsnObject {
	YCLASS(ObjectName, AsnObject)
public:
	static const int s_type = ASNLib::OBJECT_ID;
	ASNObjId m_ObjectName;
	ObjectName();
	ObjectName(void* data, int len);
	~ObjectName();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv2-PDU
class YSNMP_API ObjectSyntax : public AsnObject {
	YCLASS(ObjectSyntax, AsnObject)
public:
	static const int s_type = ASNLib::CHOICE;
	enum ObjectSyntaxType {
		SIMPLE,
		APPLICATION_WIDE,
	};
	int m_choiceType;
	SimpleSyntax* m_simple;
	ApplicationSyntax* m_application_wide;

	ObjectSyntax();
	ObjectSyntax(void* data, int len);
	~ObjectSyntax();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv2-PDU
class YSNMP_API SimpleSyntax : public AsnObject {
	YCLASS(SimpleSyntax, AsnObject)
public:
	static const int s_type = ASNLib::CHOICE;
	enum SimpleSyntaxType {
		INTEGER_VALUE,
		STRING_VALUE,
		OBJECTID_VALUE,
	};
	int m_choiceType;
	int32_t m_integer_value;
	static const int32_t s_integer_valueMinSize = 0x80000000;
	static const int32_t s_integer_valueMaxSize = 0x7fffffff;

	static const u_int16_t s_string_valueSizeMinSize = 0x0;
	static const u_int16_t s_string_valueSizeMaxSize = 0xffff;
	OctetString m_string_value;

	ASNObjId m_objectID_value;

	SimpleSyntax();
	SimpleSyntax(void* data, int len);
	~SimpleSyntax();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv2-PDU
class YSNMP_API ApplicationSyntax : public AsnObject {
	YCLASS(ApplicationSyntax, AsnObject)
public:
	static const int s_type = ASNLib::CHOICE;
	enum ApplicationSyntaxType {
		IPADDRESS_VALUE,
		COUNTER_VALUE,
		TIMETICKS_VALUE,
		ARBITRARY_VALUE,
		BIG_COUNTER_VALUE,
		UNSIGNED_INTEGER_VALUE,
	};
	int m_choiceType;
	IpAddress* m_ipAddress_value;
	Counter32* m_counter_value;
	TimeTicks* m_timeticks_value;
	Opaque* m_arbitrary_value;
	Counter64* m_big_counter_value;
	Unsigned32* m_unsigned_integer_value;

	ApplicationSyntax();
	ApplicationSyntax(void* data, int len);
	~ApplicationSyntax();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv2-PDU
class YSNMP_API IpAddress : public AsnObject {
	YCLASS(IpAddress, AsnObject)
public:
	static const int s_type = ASNLib::OCTET_STRING;
	static const int tag_IpAddress = 0x40;
	static const u_int8_t s_IpAddressSize = 0x4;
	OctetString m_IpAddress;

	IpAddress();
	IpAddress(void* data, int len);
	~IpAddress();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv2-PDU
class YSNMP_API Counter32 : public AsnObject {
	YCLASS(Counter32, AsnObject)
public:
	static const int s_type = ASNLib::INTEGER;
	static const int tag_Counter32 = 0x41;
	u_int32_t m_Counter32;
	static const u_int32_t s_Counter32MinSize = 0x0;
	static const u_int32_t s_Counter32MaxSize = 0xffffffff;

	Counter32();
	Counter32(void* data, int len);
	~Counter32();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv2-PDU
class YSNMP_API Unsigned32 : public AsnObject {
	YCLASS(Unsigned32, AsnObject)
public:
	static const int s_type = ASNLib::INTEGER;
	static const int tag_Unsigned32 = 0x42;
	u_int32_t m_Unsigned32;
	static const u_int32_t s_Unsigned32MinSize = 0x0;
	static const u_int32_t s_Unsigned32MaxSize = 0xffffffff;

	Unsigned32();
	Unsigned32(void* data, int len);
	~Unsigned32();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv2-PDU
class YSNMP_API Gauge32 : public AsnObject {
	YCLASS(Gauge32, AsnObject)
public:
	static const int s_type = ASNLib::DEFINED;
	Unsigned32* m_Gauge32;
	Gauge32();
	Gauge32(void* data, int len);
	~Gauge32();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv2-PDU
class YSNMP_API TimeTicks : public AsnObject {
	YCLASS(TimeTicks, AsnObject)
public:
	static const int s_type = ASNLib::INTEGER;
	static const int tag_TimeTicks = 0x43;
	u_int32_t m_TimeTicks;
	static const u_int32_t s_TimeTicksMinSize = 0x0;
	static const u_int32_t s_TimeTicksMaxSize = 0xffffffff;

	TimeTicks();
	TimeTicks(void* data, int len);
	~TimeTicks();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv2-PDU
class YSNMP_API Opaque : public AsnObject {
	YCLASS(Opaque, AsnObject)
public:
	static const int s_type = ASNLib::OCTET_STRING;
	static const int tag_Opaque = 0x44;
	OctetString m_Opaque;

	Opaque();
	Opaque(void* data, int len);
	~Opaque();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv2-PDU
class YSNMP_API Counter64 : public AsnObject {
	YCLASS(Counter64, AsnObject)
public:
	static const int s_type = ASNLib::INTEGER;
	static const int tag_Counter64 = 0x46;
	u_int64_t m_Counter64;
	static const u_int64_t s_Counter64MinSize = 0x0;
	static const u_int64_t s_Counter64MaxSize = 0xffffffffffffffffULL;

	Counter64();
	Counter64(void* data, int len);
	~Counter64();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv2-PDU
class YSNMP_API PDUs : public AsnObject {
	YCLASS(PDUs, AsnObject)
public:
	static const int s_type = ASNLib::CHOICE;
	enum PDUsType {
		GET_REQUEST,
		GET_NEXT_REQUEST,
		GET_BULK_REQUEST,
		RESPONSE,
		SET_REQUEST,
		INFORM_REQUEST,
		SNMPV2_TRAP,
		REPORT,
	};
	int m_choiceType;
	GetRequest_PDU* m_get_request;
	GetNextRequest_PDU* m_get_next_request;
	GetBulkRequest_PDU* m_get_bulk_request;
	Response_PDU* m_response;
	SetRequest_PDU* m_set_request;
	InformRequest_PDU* m_inform_request;
	SNMPv2_Trap_PDU* m_snmpV2_trap;
	Report_PDU* m_report;

	PDUs();
	PDUs(void* data, int len);
	~PDUs();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv2-PDU
class YSNMP_API GetRequest_PDU : public AsnObject {
	YCLASS(GetRequest_PDU, AsnObject)
public:
	static const int s_type = ASNLib::DEFINED;
	static const int tag_GetRequest_PDU = 0xa0;
	PDU* m_GetRequest_PDU;
	GetRequest_PDU();
	GetRequest_PDU(void* data, int len);
	~GetRequest_PDU();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv2-PDU
class YSNMP_API GetNextRequest_PDU : public AsnObject {
	YCLASS(GetNextRequest_PDU, AsnObject)
public:
	static const int s_type = ASNLib::DEFINED;
	static const int tag_GetNextRequest_PDU = 0xa1;
	PDU* m_GetNextRequest_PDU;
	GetNextRequest_PDU();
	GetNextRequest_PDU(void* data, int len);
	~GetNextRequest_PDU();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv2-PDU
class YSNMP_API Response_PDU : public AsnObject {
	YCLASS(Response_PDU, AsnObject)
public:
	static const int s_type = ASNLib::DEFINED;
	static const int tag_Response_PDU = 0xa2;
	PDU* m_Response_PDU;
	Response_PDU();
	Response_PDU(void* data, int len);
	~Response_PDU();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv2-PDU
class YSNMP_API SetRequest_PDU : public AsnObject {
	YCLASS(SetRequest_PDU, AsnObject)
public:
	static const int s_type = ASNLib::DEFINED;
	static const int tag_SetRequest_PDU = 0xa3;
	PDU* m_SetRequest_PDU;
	SetRequest_PDU();
	SetRequest_PDU(void* data, int len);
	~SetRequest_PDU();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv2-PDU
class YSNMP_API GetBulkRequest_PDU : public AsnObject {
	YCLASS(GetBulkRequest_PDU, AsnObject)
public:
	static const int s_type = ASNLib::DEFINED;
	static const int tag_GetBulkRequest_PDU = 0xa5;
	BulkPDU* m_GetBulkRequest_PDU;
	GetBulkRequest_PDU();
	GetBulkRequest_PDU(void* data, int len);
	~GetBulkRequest_PDU();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv2-PDU
class YSNMP_API InformRequest_PDU : public AsnObject {
	YCLASS(InformRequest_PDU, AsnObject)
public:
	static const int s_type = ASNLib::DEFINED;
	static const int tag_InformRequest_PDU = 0xa6;
	PDU* m_InformRequest_PDU;
	InformRequest_PDU();
	InformRequest_PDU(void* data, int len);
	~InformRequest_PDU();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv2-PDU
class YSNMP_API SNMPv2_Trap_PDU : public AsnObject {
	YCLASS(SNMPv2_Trap_PDU, AsnObject)
public:
	static const int s_type = ASNLib::DEFINED;
	static const int tag_SNMPv2_Trap_PDU = 0xa7;
	PDU* m_SNMPv2_Trap_PDU;
	SNMPv2_Trap_PDU();
	SNMPv2_Trap_PDU(void* data, int len);
	~SNMPv2_Trap_PDU();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv2-PDU
class YSNMP_API Report_PDU : public AsnObject {
	YCLASS(Report_PDU, AsnObject)
public:
	static const int s_type = ASNLib::DEFINED;
	static const int tag_Report_PDU = 0xa8;
	PDU* m_Report_PDU;
	Report_PDU();
	Report_PDU(void* data, int len);
	~Report_PDU();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv2-PDU
class YSNMP_API PDU : public AsnObject {
	YCLASS(PDU, AsnObject)
public:
	static const int s_type = ASNLib::SEQUENCE;
	int32_t m_request_id;
	static const int32_t s_request_idMinSize = -0xccd56a0;
	static const int32_t s_request_idMaxSize = 0xccd569f;

	int32_t m_error_status;
	static const int32_t s_noError_error_status = 0x0;
	static const int32_t s_tooBig_error_status = 0x1;
	static const int32_t s_noSuchName_error_status = 0x2;
	static const int32_t s_badValue_error_status = 0x3;
	static const int32_t s_readOnly_error_status = 0x4;
	static const int32_t s_genErr_error_status = 0x5;
	static const int32_t s_noAccess_error_status = 0x6;
	static const int32_t s_wrongType_error_status = 0x7;
	static const int32_t s_wrongLength_error_status = 0x8;
	static const int32_t s_wrongEncoding_error_status = 0x9;
	static const int32_t s_wrongValue_error_status = 0xa;
	static const int32_t s_noCreation_error_status = 0xb;
	static const int32_t s_inconsistentValue_error_status = 0xc;
	static const int32_t s_resourceUnavailable_error_status = 0xd;
	static const int32_t s_commitFailed_error_status = 0xe;
	static const int32_t s_undoFailed_error_status = 0xf;
	static const int32_t s_authorizationError_error_status = 0x10;
	static const int32_t s_notWritable_error_status = 0x11;
	static const int32_t s_inconsistentName_error_status = 0x12;

	int32_t m_error_index;
	static const int32_t s_error_indexMinSize = 0x0;
	static const int32_t s_error_indexMaxSize = 0x7fffffff;

	VarBindList* m_variable_bindings;

	PDU();
	PDU(void* data, int len);
	~PDU();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv2-PDU
class YSNMP_API BulkPDU : public AsnObject {
	YCLASS(BulkPDU, AsnObject)
public:
	static const int s_type = ASNLib::SEQUENCE;
	int32_t m_request_id;
	static const int32_t s_request_idMinSize = -0xccd56a0;
	static const int32_t s_request_idMaxSize = 0xccd569f;

	int32_t m_non_repeaters;
	static const int32_t s_non_repeatersMinSize = 0x0;
	static const int32_t s_non_repeatersMaxSize = 0x7fffffff;

	int32_t m_max_repetitions;
	static const int32_t s_max_repetitionsMinSize = 0x0;
	static const int32_t s_max_repetitionsMaxSize = 0x7fffffff;

	VarBindList* m_variable_bindings;

	BulkPDU();
	BulkPDU(void* data, int len);
	~BulkPDU();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv2-PDU
class YSNMP_API VarBind : public AsnObject {
	YCLASS(VarBind, AsnObject)
public:
	static const int s_type = ASNLib::SEQUENCE;
	ObjectName* m_name;
	enum AsnChoiceTypeType {
		VALUE,
		UNSPECIFIED,
		NOSUCHOBJECT,
		NOSUCHINSTANCE,
		ENDOFMIBVIEW,
	};
	int m_choiceType;
	ObjectSyntax* m_value;
	int  m_unSpecified;

	static const int tag_noSuchObject = 0x80;
	int  m_noSuchObject;

	static const int tag_noSuchInstance = 0x81;
	int  m_noSuchInstance;

	static const int tag_endOfMibView = 0x82;
	int  m_endOfMibView;

	VarBind();
	VarBind(void* data, int len);
	~VarBind();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv2-PDU
class YSNMP_API VarBindList : public AsnObject {
	YCLASS(VarBindList, AsnObject)
public:
	static const int s_type = ASNLib::SEQUENCE;
	ObjList m_list;
	// object list type = VarBind*

	VarBindList();
	VarBindList(void* data, int len);
	~VarBindList();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv2-TC
class YSNMP_API DisplayString : public AsnObject {
	YCLASS(DisplayString, AsnObject)
public:
	static const int s_type = ASNLib::OCTET_STRING;
	static const u_int8_t s_DisplayStringSizeMinSize = 0x0;
	static const u_int8_t s_DisplayStringSizeMaxSize = 0xff;
	OctetString m_DisplayString;

	DisplayString();
	DisplayString(void* data, int len);
	~DisplayString();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv2-TC
class YSNMP_API PhysAddress : public AsnObject {
	YCLASS(PhysAddress, AsnObject)
public:
	static const int s_type = ASNLib::OCTET_STRING;
	OctetString m_PhysAddress;

	PhysAddress();
	PhysAddress(void* data, int len);
	~PhysAddress();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv2-TC
class YSNMP_API MacAddress : public AsnObject {
	YCLASS(MacAddress, AsnObject)
public:
	static const int s_type = ASNLib::OCTET_STRING;
	static const u_int8_t s_MacAddressSize = 0x6;
	OctetString m_MacAddress;

	MacAddress();
	MacAddress(void* data, int len);
	~MacAddress();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv2-TC
class YSNMP_API TruthValue : public AsnObject {
	YCLASS(TruthValue, AsnObject)
public:
	static const int s_type = ASNLib::INTEGER;
	int32_t m_TruthValue;
	static const int32_t s_true_TruthValue = 0x1;
	static const int32_t s_false_TruthValue = 0x2;

	TruthValue();
	TruthValue(void* data, int len);
	~TruthValue();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv2-TC
class YSNMP_API TestAndIncr : public AsnObject {
	YCLASS(TestAndIncr, AsnObject)
public:
	static const int s_type = ASNLib::INTEGER;
	u_int32_t m_TestAndIncr;
	static const u_int32_t s_TestAndIncrMinSize = 0x0;
	static const u_int32_t s_TestAndIncrMaxSize = 0x7fffffff;

	TestAndIncr();
	TestAndIncr(void* data, int len);
	~TestAndIncr();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv2-TC
class YSNMP_API AutonomousType : public AsnObject {
	YCLASS(AutonomousType, AsnObject)
public:
	static const int s_type = ASNLib::OBJECT_ID;
	ASNObjId m_AutonomousType;
	AutonomousType();
	AutonomousType(void* data, int len);
	~AutonomousType();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv2-TC
class YSNMP_API InstancePointer : public AsnObject {
	YCLASS(InstancePointer, AsnObject)
public:
	static const int s_type = ASNLib::OBJECT_ID;
	ASNObjId m_InstancePointer;
	InstancePointer();
	InstancePointer(void* data, int len);
	~InstancePointer();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv2-TC
class YSNMP_API VariablePointer : public AsnObject {
	YCLASS(VariablePointer, AsnObject)
public:
	static const int s_type = ASNLib::OBJECT_ID;
	ASNObjId m_VariablePointer;
	VariablePointer();
	VariablePointer(void* data, int len);
	~VariablePointer();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv2-TC
class YSNMP_API RowPointer : public AsnObject {
	YCLASS(RowPointer, AsnObject)
public:
	static const int s_type = ASNLib::OBJECT_ID;
	ASNObjId m_RowPointer;
	RowPointer();
	RowPointer(void* data, int len);
	~RowPointer();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv2-TC
class YSNMP_API RowStatus : public AsnObject {
	YCLASS(RowStatus, AsnObject)
public:
	static const int s_type = ASNLib::INTEGER;
	int32_t m_RowStatus;
	static const int32_t s_active_RowStatus = 0x1;
	static const int32_t s_notInService_RowStatus = 0x2;
	static const int32_t s_notReady_RowStatus = 0x3;
	static const int32_t s_createAndGo_RowStatus = 0x4;
	static const int32_t s_createAndWait_RowStatus = 0x5;
	static const int32_t s_destroy_RowStatus = 0x6;

	RowStatus();
	RowStatus(void* data, int len);
	~RowStatus();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv2-TC
class YSNMP_API TimeStamp : public AsnObject {
	YCLASS(TimeStamp, AsnObject)
public:
	static const int s_type = ASNLib::DEFINED;
	TimeTicks* m_TimeStamp;
	TimeStamp();
	TimeStamp(void* data, int len);
	~TimeStamp();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv2-TC
class YSNMP_API TimeInterval : public AsnObject {
	YCLASS(TimeInterval, AsnObject)
public:
	static const int s_type = ASNLib::INTEGER;
	u_int32_t m_TimeInterval;
	static const u_int32_t s_TimeIntervalMinSize = 0x0;
	static const u_int32_t s_TimeIntervalMaxSize = 0x7fffffff;

	TimeInterval();
	TimeInterval(void* data, int len);
	~TimeInterval();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv2-TC
class YSNMP_API DateAndTime : public AsnObject {
	YCLASS(DateAndTime, AsnObject)
public:
	static const int s_type = ASNLib::OCTET_STRING;
	static const u_int8_t s_DateAndTimeSize_0 = 0x8;
	static const u_int8_t s_DateAndTimeSize_1 = 0xb;
	OctetString m_DateAndTime;

	DateAndTime();
	DateAndTime(void* data, int len);
	~DateAndTime();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv2-TC
class YSNMP_API StorageType : public AsnObject {
	YCLASS(StorageType, AsnObject)
public:
	static const int s_type = ASNLib::INTEGER;
	int32_t m_StorageType;
	static const int32_t s_other_StorageType = 0x1;
	static const int32_t s_volatile_StorageType = 0x2;
	static const int32_t s_nonVolatile_StorageType = 0x3;
	static const int32_t s_permanent_StorageType = 0x4;
	static const int32_t s_readOnly_StorageType = 0x5;

	StorageType();
	StorageType(void* data, int len);
	~StorageType();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv2-TC
class YSNMP_API TDomain : public AsnObject {
	YCLASS(TDomain, AsnObject)
public:
	static const int s_type = ASNLib::OBJECT_ID;
	ASNObjId m_TDomain;
	TDomain();
	TDomain(void* data, int len);
	~TDomain();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv2-TC
class YSNMP_API TAddress : public AsnObject {
	YCLASS(TAddress, AsnObject)
public:
	static const int s_type = ASNLib::OCTET_STRING;
	static const u_int8_t s_TAddressSizeMinSize = 0x1;
	static const u_int8_t s_TAddressSizeMaxSize = 0xff;
	OctetString m_TAddress;

	TAddress();
	TAddress(void* data, int len);
	~TAddress();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv3MessageSyntax
class YSNMP_API SNMPv3Message : public AsnObject {
	YCLASS(SNMPv3Message, AsnObject)
public:
	static const int s_type = ASNLib::SEQUENCE;
	u_int32_t m_msgVersion;
	static const u_int32_t s_msgVersionMinSize = 0x0;
	static const u_int32_t s_msgVersionMaxSize = 0x7fffffff;

	HeaderData* m_msgGlobalData;
	OctetString m_msgSecurityParameters;

	ScopedPduData* m_msgData;

	SNMPv3Message();
	SNMPv3Message(void* data, int len);
	~SNMPv3Message();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv3MessageSyntax
class YSNMP_API HeaderData : public AsnObject {
	YCLASS(HeaderData, AsnObject)
public:
	static const int s_type = ASNLib::SEQUENCE;
	u_int32_t m_msgID;
	static const u_int32_t s_msgIDMinSize = 0x0;
	static const u_int32_t s_msgIDMaxSize = 0x7fffffff;

	u_int32_t m_msgMaxSize;
	static const u_int32_t s_msgMaxSizeMinSize = 0x1e4;
	static const u_int32_t s_msgMaxSizeMaxSize = 0x7fffffff;

	static const u_int8_t s_msgFlagsSize = 0x1;
	OctetString m_msgFlags;

	u_int32_t m_msgSecurityModel;
	static const u_int32_t s_msgSecurityModelMinSize = 0x1;
	static const u_int32_t s_msgSecurityModelMaxSize = 0x7fffffff;


	HeaderData();
	HeaderData(void* data, int len);
	~HeaderData();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv3MessageSyntax
class YSNMP_API ScopedPduData : public AsnObject {
	YCLASS(ScopedPduData, AsnObject)
public:
	static const int s_type = ASNLib::CHOICE;
	enum ScopedPduDataType {
		PLAINTEXT,
		ENCRYPTEDPDU,
	};
	int m_choiceType;
	ScopedPDU* m_plaintext;
	OctetString m_encryptedPDU;


	ScopedPduData();
	ScopedPduData(void* data, int len);
	~ScopedPduData();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv3MessageSyntax
class YSNMP_API ScopedPDU : public AsnObject {
	YCLASS(ScopedPDU, AsnObject)
public:
	static const int s_type = ASNLib::SEQUENCE;
	OctetString m_contextEngineID;

	OctetString m_contextName;

	DataBlock m_data;

	ScopedPDU();
	ScopedPDU(void* data, int len);
	~ScopedPDU();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in COMMUNITY-BASED-SNMPv2
class YSNMP_API Message : public AsnObject {
	YCLASS(Message, AsnObject)
public:
	static const int s_type = ASNLib::SEQUENCE;
	int32_t m_version;
	static const int32_t s_version_1_version = 0x0;
	static const int32_t s_version_2_version = 0x1;

	OctetString m_community;

	DataBlock m_data;

	Message();
	Message(void* data, int len);
	~Message();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMP-USER-BASED-SM-MIB
class YSNMP_API KeyChange : public AsnObject {
	YCLASS(KeyChange, AsnObject)
public:
	static const int s_type = ASNLib::OCTET_STRING;
	OctetString m_KeyChange;

	KeyChange();
	KeyChange(void* data, int len);
	~KeyChange();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMP-USER-BASED-SM-MIB
class YSNMP_API UsmUserEntry : public AsnObject {
	YCLASS(UsmUserEntry, AsnObject)
public:
	static const int s_type = ASNLib::SEQUENCE;
	SnmpEngineID* m_usmUserEngineID;
	SnmpAdminString* m_usmUserName;
	SnmpAdminString* m_usmUserSecurityName;
	RowPointer* m_usmUserCloneFrom;
	AutonomousType* m_usmUserAuthProtocol;
	KeyChange* m_usmUserAuthKeyChange;
	KeyChange* m_usmUserOwnAuthKeyChange;
	AutonomousType* m_usmUserPrivProtocol;
	KeyChange* m_usmUserPrivKeyChange;
	KeyChange* m_usmUserOwnPrivKeyChange;
	OctetString m_usmUserPublic;

	StorageType* m_usmUserStorageType;
	RowStatus* m_usmUserStatus;

	UsmUserEntry();
	UsmUserEntry(void* data, int len);
	~UsmUserEntry();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMP-FRAMEWORK-MIB
class YSNMP_API SnmpEngineID : public AsnObject {
	YCLASS(SnmpEngineID, AsnObject)
public:
	static const int s_type = ASNLib::OCTET_STRING;
	static const u_int8_t s_SnmpEngineIDSizeMinSize = 0x5;
	static const u_int8_t s_SnmpEngineIDSizeMaxSize = 0x20;
	OctetString m_SnmpEngineID;

	SnmpEngineID();
	SnmpEngineID(void* data, int len);
	~SnmpEngineID();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMP-FRAMEWORK-MIB
class YSNMP_API SnmpSecurityModel : public AsnObject {
	YCLASS(SnmpSecurityModel, AsnObject)
public:
	static const int s_type = ASNLib::INTEGER;
	u_int32_t m_SnmpSecurityModel;
	static const u_int32_t s_SnmpSecurityModelMinSize = 0x0;
	static const u_int32_t s_SnmpSecurityModelMaxSize = 0x7fffffff;

	SnmpSecurityModel();
	SnmpSecurityModel(void* data, int len);
	~SnmpSecurityModel();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMP-FRAMEWORK-MIB
class YSNMP_API SnmpMessageProcessingModel : public AsnObject {
	YCLASS(SnmpMessageProcessingModel, AsnObject)
public:
	static const int s_type = ASNLib::INTEGER;
	u_int32_t m_SnmpMessageProcessingModel;
	static const u_int32_t s_SnmpMessageProcessingModelMinSize = 0x0;
	static const u_int32_t s_SnmpMessageProcessingModelMaxSize = 0x7fffffff;

	SnmpMessageProcessingModel();
	SnmpMessageProcessingModel(void* data, int len);
	~SnmpMessageProcessingModel();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMP-FRAMEWORK-MIB
class YSNMP_API SnmpSecurityLevel : public AsnObject {
	YCLASS(SnmpSecurityLevel, AsnObject)
public:
	static const int s_type = ASNLib::INTEGER;
	int32_t m_SnmpSecurityLevel;
	static const int32_t s_noAuthNoPriv_SnmpSecurityLevel = 0x1;
	static const int32_t s_authNoPriv_SnmpSecurityLevel = 0x2;
	static const int32_t s_authPriv_SnmpSecurityLevel = 0x3;

	SnmpSecurityLevel();
	SnmpSecurityLevel(void* data, int len);
	~SnmpSecurityLevel();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMP-FRAMEWORK-MIB
class YSNMP_API SnmpAdminString : public AsnObject {
	YCLASS(SnmpAdminString, AsnObject)
public:
	static const int s_type = ASNLib::OCTET_STRING;
	static const u_int8_t s_SnmpAdminStringSizeMinSize = 0x0;
	static const u_int8_t s_SnmpAdminStringSizeMaxSize = 0xff;
	OctetString m_SnmpAdminString;

	SnmpAdminString();
	SnmpAdminString(void* data, int len);
	~SnmpAdminString();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in USMSecurityParametersSyntax
class YSNMP_API UsmSecurityParameters : public AsnObject {
	YCLASS(UsmSecurityParameters, AsnObject)
public:
	static const int s_type = ASNLib::SEQUENCE;
	OctetString m_msgAuthoritativeEngineID;

	u_int32_t m_msgAuthoritativeEngineBoots;
	static const u_int32_t s_msgAuthoritativeEngineBootsMinSize = 0x0;
	static const u_int32_t s_msgAuthoritativeEngineBootsMaxSize = 0x7fffffff;

	u_int32_t m_msgAuthoritativeEngineTime;
	static const u_int32_t s_msgAuthoritativeEngineTimeMinSize = 0x0;
	static const u_int32_t s_msgAuthoritativeEngineTimeMaxSize = 0x7fffffff;

	static const u_int8_t s_msgUserNameSizeMinSize = 0x0;
	static const u_int8_t s_msgUserNameSizeMaxSize = 0x20;
	OctetString m_msgUserName;

	OctetString m_msgAuthenticationParameters;

	OctetString m_msgPrivacyParameters;


	UsmSecurityParameters();
	UsmSecurityParameters(void* data, int len);
	~UsmSecurityParameters();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

// defined in SNMPv2-MIB
class YSNMP_API YSNMP_API SysOREntry : public AsnObject {
	YCLASS(SysOREntry, AsnObject)
public:
	static const int s_type = ASNLib::SEQUENCE;
	int32_t m_sysORIndex;

	ASNObjId m_sysORID;
	DisplayString* m_sysORDescr;
	TimeStamp* m_sysORUpTime;

	SysOREntry();
	SysOREntry(void* data, int len);
	~SysOREntry();
	int decode(DataBlock& data);
	int encode(DataBlock& data);
	void getParams(NamedList* params);
	void setParams(NamedList* params);
};

}

#endif /* __YATESNMP_H */
