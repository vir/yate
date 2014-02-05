// generated on Sep 17, 2010 4:11 PM

#include "yatesnmp.h"

namespace Snmp {

/**
  * ObjectName
  */
ObjectName::ObjectName()
{
}

ObjectName::ObjectName(void* data, int len)
{
	DataBlock db(data,len);
	decode(db);
}

ObjectName::~ObjectName()
{
}

int ObjectName::decode(DataBlock& data)
{
	int length = 0;
	length = ASNLib::decodeOID(data,&m_ObjectName,true);
	return length;
}

int ObjectName::encode(DataBlock& data)
{
	int length = -1;
	const DataBlock db = ASNLib::encodeOID(m_ObjectName,true);
	data.append(db);
	length = db.length();
	return length;
}

void ObjectName::getParams(NamedList* params)
{}

void ObjectName::setParams(NamedList* params)
{}

/**
  * ObjectSyntax
  */
ObjectSyntax::ObjectSyntax()
{
	m_simple = new SimpleSyntax();
	m_application_wide = new ApplicationSyntax();
}

ObjectSyntax::ObjectSyntax(void* data, int len)
{
	m_simple = new SimpleSyntax();
	m_application_wide = new ApplicationSyntax();

	DataBlock db(data,len);
	decode(db);
}

ObjectSyntax::~ObjectSyntax()
{
	TelEngine::destruct(m_simple);
	TelEngine::destruct(m_application_wide);
}

int ObjectSyntax::decode(DataBlock& data)
{
	int length = 0;
	length = m_simple->decode(data);
	if (length >= 0) {
		m_choiceType = SIMPLE;
		return length;
	}
	length = m_application_wide->decode(data);
	if (length >= 0) {
		m_choiceType = APPLICATION_WIDE;
		return length;
	}
	return length;
}

int ObjectSyntax::encode(DataBlock& data)
{
	int length = -1;
	if (m_choiceType == SIMPLE) {
		length = m_simple->encode(data);
	}
	if (m_choiceType == APPLICATION_WIDE) {
		length = m_application_wide->encode(data);
	}
	return length;
}

void ObjectSyntax::getParams(NamedList* params)
{}

void ObjectSyntax::setParams(NamedList* params)
{}

/**
  * SimpleSyntax
  */
SimpleSyntax::SimpleSyntax()
{
}

SimpleSyntax::SimpleSyntax(void* data, int len)
{
	DataBlock db(data,len);
	decode(db);
}

SimpleSyntax::~SimpleSyntax()
{
}

int SimpleSyntax::decode(DataBlock& data)
{
	int length = 0;
	length = ASNLib::decodeINT32(data,&m_integer_value,true);
	if (m_integer_value < s_integer_valueMinSize)
		DDebug(DebugAll,"Constraint break error");
	if (s_integer_valueMaxSize < m_integer_value)
		DDebug(DebugAll,"Constraint break error");
	if (length >= 0) {
		m_choiceType = INTEGER_VALUE;
		return length;
	}
	length = ASNLib::decodeOctetString(data,&m_string_value,true);
	if (length < s_string_valueSizeMinSize)
		DDebug(DebugAll,"Constraint break error");
	if (s_string_valueSizeMaxSize < length)
		DDebug(DebugAll,"Constraint break error");
	if (length >= 0) {
		m_choiceType = STRING_VALUE;
		return length;
	}
	length = ASNLib::decodeOID(data,&m_objectID_value,true);
	if (length >= 0) {
		m_choiceType = OBJECTID_VALUE;
		return length;
	}
	return length;
}

int SimpleSyntax::encode(DataBlock& data)
{
	int length = -1;
	if (m_choiceType == INTEGER_VALUE) {
		const DataBlock db = ASNLib::encodeInteger(m_integer_value,true);
		data.append(db);
		length = db.length();
	}
	if (m_choiceType == STRING_VALUE) {
		const DataBlock db = ASNLib::encodeOctetString(m_string_value,true);
		data.append(db);
		length = db.length();
	}
	if (m_choiceType == OBJECTID_VALUE) {
		const DataBlock db = ASNLib::encodeOID(m_objectID_value,true);
		data.append(db);
		length = db.length();
	}
	return length;
}

void SimpleSyntax::getParams(NamedList* params)
{}

void SimpleSyntax::setParams(NamedList* params)
{}

/**
  * ApplicationSyntax
  */
ApplicationSyntax::ApplicationSyntax()
{
	m_ipAddress_value = new IpAddress();
	m_counter_value = new Counter32();
	m_timeticks_value = new TimeTicks();
	m_arbitrary_value = new Opaque();
	m_big_counter_value = new Counter64();
	m_unsigned_integer_value = new Unsigned32();
}

ApplicationSyntax::ApplicationSyntax(void* data, int len)
{
	m_ipAddress_value = new IpAddress();
	m_counter_value = new Counter32();
	m_timeticks_value = new TimeTicks();
	m_arbitrary_value = new Opaque();
	m_big_counter_value = new Counter64();
	m_unsigned_integer_value = new Unsigned32();

	DataBlock db(data,len);
	decode(db);
}

ApplicationSyntax::~ApplicationSyntax()
{
	TelEngine::destruct(m_ipAddress_value);
	TelEngine::destruct(m_counter_value);
	TelEngine::destruct(m_timeticks_value);
	TelEngine::destruct(m_arbitrary_value);
	TelEngine::destruct(m_big_counter_value);
	TelEngine::destruct(m_unsigned_integer_value);
}

int ApplicationSyntax::decode(DataBlock& data)
{
	int length = 0;
	length = m_ipAddress_value->decode(data);
	if (length >= 0) {
		m_choiceType = IPADDRESS_VALUE;
		return length;
	}
	length = m_counter_value->decode(data);
	if (length >= 0) {
		m_choiceType = COUNTER_VALUE;
		return length;
	}
	length = m_timeticks_value->decode(data);
	if (length >= 0) {
		m_choiceType = TIMETICKS_VALUE;
		return length;
	}
	length = m_arbitrary_value->decode(data);
	if (length >= 0) {
		m_choiceType = ARBITRARY_VALUE;
		return length;
	}
	length = m_big_counter_value->decode(data);
	if (length >= 0) {
		m_choiceType = BIG_COUNTER_VALUE;
		return length;
	}
	length = m_unsigned_integer_value->decode(data);
	if (length >= 0) {
		m_choiceType = UNSIGNED_INTEGER_VALUE;
		return length;
	}
	return length;
}

int ApplicationSyntax::encode(DataBlock& data)
{
	int length = -1;
	if (m_choiceType == IPADDRESS_VALUE) {
		length = m_ipAddress_value->encode(data);
	}
	if (m_choiceType == COUNTER_VALUE) {
		length = m_counter_value->encode(data);
	}
	if (m_choiceType == TIMETICKS_VALUE) {
		length = m_timeticks_value->encode(data);
	}
	if (m_choiceType == ARBITRARY_VALUE) {
		length = m_arbitrary_value->encode(data);
	}
	if (m_choiceType == BIG_COUNTER_VALUE) {
		length = m_big_counter_value->encode(data);
	}
	if (m_choiceType == UNSIGNED_INTEGER_VALUE) {
		length = m_unsigned_integer_value->encode(data);
	}
	return length;
}

void ApplicationSyntax::getParams(NamedList* params)
{}

void ApplicationSyntax::setParams(NamedList* params)
{}

/**
  * IpAddress
  */
IpAddress::IpAddress()
{
}

IpAddress::IpAddress(void* data, int len)
{
	DataBlock db(data,len);
	decode(db);
}

IpAddress::~IpAddress()
{
}

int IpAddress::decode(DataBlock& data)
{
	int length = 0;
	length = -1;
	if (data.length() < 2)
		return ASNLib::InvalidLengthOrTag;
	if (data[0] == tag_IpAddress) {
		data.cut(-1);
		length = ASNLib::decodeOctetString(data,&m_IpAddress,false);
		if (length != s_IpAddressSize)
			DDebug(DebugAll,"Constraint break error");
	}
	return length;
}

int IpAddress::encode(DataBlock& data)
{
	int length = -1;
	DataBlock contents;
	u_int8_t tag = tag_IpAddress;
	const DataBlock db = ASNLib::encodeOctetString(m_IpAddress,false);
	contents.append(db);
	length = db.length();
	DataBlock len = ASNLib::buildLength(contents);
	data.append(&tag,1);
	data.append(len);
	data.append(contents);
	return length;
}

void IpAddress::getParams(NamedList* params)
{}

void IpAddress::setParams(NamedList* params)
{}

/**
  * Counter32
  */
Counter32::Counter32()
{
}

Counter32::Counter32(void* data, int len)
{
	DataBlock db(data,len);
	decode(db);
}

Counter32::~Counter32()
{
}

int Counter32::decode(DataBlock& data)
{
	int length = 0;
	length = -1;
	if (data.length() < 2)
		return ASNLib::InvalidLengthOrTag;
	if (data[0] == tag_Counter32) {
		data.cut(-1);
		length = ASNLib::decodeUINT32(data,&m_Counter32,false);
		if (m_Counter32 < s_Counter32MinSize)
			DDebug(DebugAll,"Constraint break error");
		if (s_Counter32MaxSize < m_Counter32)
			DDebug(DebugAll,"Constraint break error");
	}
	return length;
}

int Counter32::encode(DataBlock& data)
{
	int length = -1;
	DataBlock contents;
	u_int8_t tag = tag_Counter32;
	const DataBlock db = ASNLib::encodeInteger(m_Counter32,false);
	contents.append(db);
	length = db.length();
	DataBlock len = ASNLib::buildLength(contents);
	data.append(&tag,1);
	data.append(len);
	data.append(contents);
	return length;
}

void Counter32::getParams(NamedList* params)
{}

void Counter32::setParams(NamedList* params)
{}

/**
  * Unsigned32
  */
Unsigned32::Unsigned32()
{
}

Unsigned32::Unsigned32(void* data, int len)
{
	DataBlock db(data,len);
	decode(db);
}

Unsigned32::~Unsigned32()
{
}

int Unsigned32::decode(DataBlock& data)
{
	int length = 0;
	length = -1;
	if (data.length() < 2)
		return ASNLib::InvalidLengthOrTag;
	if (data[0] == tag_Unsigned32) {
		data.cut(-1);
		length = ASNLib::decodeUINT32(data,&m_Unsigned32,false);
		if (m_Unsigned32 < s_Unsigned32MinSize)
			DDebug(DebugAll,"Constraint break error");
		if (s_Unsigned32MaxSize < m_Unsigned32)
			DDebug(DebugAll,"Constraint break error");
	}
	return length;
}

int Unsigned32::encode(DataBlock& data)
{
	int length = -1;
	DataBlock contents;
	u_int8_t tag = tag_Unsigned32;
	const DataBlock db = ASNLib::encodeInteger(m_Unsigned32,false);
	contents.append(db);
	length = db.length();
	DataBlock len = ASNLib::buildLength(contents);
	data.append(&tag,1);
	data.append(len);
	data.append(contents);
	return length;
}

void Unsigned32::getParams(NamedList* params)
{}

void Unsigned32::setParams(NamedList* params)
{}

/**
  * Gauge32
  */
Gauge32::Gauge32()
{
	m_Gauge32 = new Unsigned32();
}

Gauge32::Gauge32(void* data, int len)
{
	m_Gauge32 = new Unsigned32();

	DataBlock db(data,len);
	decode(db);
}

Gauge32::~Gauge32()
{
	TelEngine::destruct(m_Gauge32);
}

int Gauge32::decode(DataBlock& data)
{
	int length = 0;
	length = m_Gauge32->decode(data);
	return length;
}

int Gauge32::encode(DataBlock& data)
{
	int length = -1;
	length = m_Gauge32->encode(data);
	return length;
}

void Gauge32::getParams(NamedList* params)
{}

void Gauge32::setParams(NamedList* params)
{}

/**
  * TimeTicks
  */
TimeTicks::TimeTicks()
{
}

TimeTicks::TimeTicks(void* data, int len)
{
	DataBlock db(data,len);
	decode(db);
}

TimeTicks::~TimeTicks()
{
}

int TimeTicks::decode(DataBlock& data)
{
	int length = 0;
	length = -1;
	if (data.length() < 2)
		return ASNLib::InvalidLengthOrTag;
	if (data[0] == tag_TimeTicks) {
		data.cut(-1);
		length = ASNLib::decodeUINT32(data,&m_TimeTicks,false);
		if (m_TimeTicks < s_TimeTicksMinSize)
			DDebug(DebugAll,"Constraint break error");
		if (s_TimeTicksMaxSize < m_TimeTicks)
			DDebug(DebugAll,"Constraint break error");
	}
	return length;
}

int TimeTicks::encode(DataBlock& data)
{
	int length = -1;
	DataBlock contents;
	u_int8_t tag = tag_TimeTicks;
	const DataBlock db = ASNLib::encodeInteger(m_TimeTicks,false);
	contents.append(db);
	length = db.length();
	DataBlock len = ASNLib::buildLength(contents);
	data.append(&tag,1);
	data.append(len);
	data.append(contents);
	return length;
}

void TimeTicks::getParams(NamedList* params)
{}

void TimeTicks::setParams(NamedList* params)
{}

/**
  * Opaque
  */
Opaque::Opaque()
{
}

Opaque::Opaque(void* data, int len)
{
	DataBlock db(data,len);
	decode(db);
}

Opaque::~Opaque()
{
}

int Opaque::decode(DataBlock& data)
{
	int length = 0;
	length = -1;
	if (data.length() < 2)
		return ASNLib::InvalidLengthOrTag;
	if (data[0] == tag_Opaque) {
		data.cut(-1);
		length = ASNLib::decodeOctetString(data,&m_Opaque,false);
	}
	return length;
}

int Opaque::encode(DataBlock& data)
{
	int length = -1;
	DataBlock contents;
	u_int8_t tag = tag_Opaque;
	const DataBlock db = ASNLib::encodeOctetString(m_Opaque,false);
	contents.append(db);
	length = db.length();
	DataBlock len = ASNLib::buildLength(contents);
	data.append(&tag,1);
	data.append(len);
	data.append(contents);
	return length;
}

void Opaque::getParams(NamedList* params)
{}

void Opaque::setParams(NamedList* params)
{}

/**
  * Counter64
  */
Counter64::Counter64()
{
}

Counter64::Counter64(void* data, int len)
{
	DataBlock db(data,len);
	decode(db);
}

Counter64::~Counter64()
{
}

int Counter64::decode(DataBlock& data)
{
	int length = 0;
	length = -1;
	if (data.length() < 2)
		return ASNLib::InvalidLengthOrTag;
	if (data[0] == tag_Counter64) {
		data.cut(-1);
		length = ASNLib::decodeUINT64(data,&m_Counter64,false);
		if (m_Counter64 < s_Counter64MinSize)
			DDebug(DebugAll,"Constraint break error");
		if (s_Counter64MaxSize < m_Counter64)
			DDebug(DebugAll,"Constraint break error");
	}
	return length;
}

int Counter64::encode(DataBlock& data)
{
	int length = -1;
	DataBlock contents;
	u_int8_t tag = tag_Counter64;
	const DataBlock db = ASNLib::encodeInteger(m_Counter64,false);
	contents.append(db);
	length = db.length();
	DataBlock len = ASNLib::buildLength(contents);
	data.append(&tag,1);
	data.append(len);
	data.append(contents);
	return length;
}

void Counter64::getParams(NamedList* params)
{}

void Counter64::setParams(NamedList* params)
{}

/**
  * PDUs
  */
PDUs::PDUs()
{
	m_get_request = new GetRequest_PDU();
	m_get_next_request = new GetNextRequest_PDU();
	m_get_bulk_request = new GetBulkRequest_PDU();
	m_response = new Response_PDU();
	m_set_request = new SetRequest_PDU();
	m_inform_request = new InformRequest_PDU();
	m_snmpV2_trap = new SNMPv2_Trap_PDU();
	m_report = new Report_PDU();
}

PDUs::PDUs(void* data, int len)
{
	m_get_request = new GetRequest_PDU();
	m_get_next_request = new GetNextRequest_PDU();
	m_get_bulk_request = new GetBulkRequest_PDU();
	m_response = new Response_PDU();
	m_set_request = new SetRequest_PDU();
	m_inform_request = new InformRequest_PDU();
	m_snmpV2_trap = new SNMPv2_Trap_PDU();
	m_report = new Report_PDU();

	DataBlock db(data,len);
	decode(db);
}

PDUs::~PDUs()
{
	TelEngine::destruct(m_get_request);
	TelEngine::destruct(m_get_next_request);
	TelEngine::destruct(m_get_bulk_request);
	TelEngine::destruct(m_response);
	TelEngine::destruct(m_set_request);
	TelEngine::destruct(m_inform_request);
	TelEngine::destruct(m_snmpV2_trap);
	TelEngine::destruct(m_report);
}

int PDUs::decode(DataBlock& data)
{
	int length = 0;
	length = m_get_request->decode(data);
	if (length >= 0) {
		m_choiceType = GET_REQUEST;
		return length;
	}
	length = m_get_next_request->decode(data);
	if (length >= 0) {
		m_choiceType = GET_NEXT_REQUEST;
		return length;
	}
	length = m_get_bulk_request->decode(data);
	if (length >= 0) {
		m_choiceType = GET_BULK_REQUEST;
		return length;
	}
	length = m_response->decode(data);
	if (length >= 0) {
		m_choiceType = RESPONSE;
		return length;
	}
	length = m_set_request->decode(data);
	if (length >= 0) {
		m_choiceType = SET_REQUEST;
		return length;
	}
	length = m_inform_request->decode(data);
	if (length >= 0) {
		m_choiceType = INFORM_REQUEST;
		return length;
	}
	length = m_snmpV2_trap->decode(data);
	if (length >= 0) {
		m_choiceType = SNMPV2_TRAP;
		return length;
	}
	length = m_report->decode(data);
	if (length >= 0) {
		m_choiceType = REPORT;
		return length;
	}
	return length;
}

int PDUs::encode(DataBlock& data)
{
	int length = -1;
	if (m_choiceType == GET_REQUEST) {
		length = m_get_request->encode(data);
	}
	if (m_choiceType == GET_NEXT_REQUEST) {
		length = m_get_next_request->encode(data);
	}
	if (m_choiceType == GET_BULK_REQUEST) {
		length = m_get_bulk_request->encode(data);
	}
	if (m_choiceType == RESPONSE) {
		length = m_response->encode(data);
	}
	if (m_choiceType == SET_REQUEST) {
		length = m_set_request->encode(data);
	}
	if (m_choiceType == INFORM_REQUEST) {
		length = m_inform_request->encode(data);
	}
	if (m_choiceType == SNMPV2_TRAP) {
		length = m_snmpV2_trap->encode(data);
	}
	if (m_choiceType == REPORT) {
		length = m_report->encode(data);
	}
	return length;
}

void PDUs::getParams(NamedList* params)
{}

void PDUs::setParams(NamedList* params)
{}

/**
  * GetRequest_PDU
  */
GetRequest_PDU::GetRequest_PDU()
{
	m_GetRequest_PDU = new PDU();
}

GetRequest_PDU::GetRequest_PDU(void* data, int len)
{
	m_GetRequest_PDU = new PDU();
	DataBlock db(data,len);
	decode(db);
}

GetRequest_PDU::~GetRequest_PDU()
{
	TelEngine::destruct(m_GetRequest_PDU);
}

int GetRequest_PDU::decode(DataBlock& data)
{
	int length = 0;
	length = -1;
	if (data.length() < 2)
		return ASNLib::InvalidLengthOrTag;
	if (data[0] == tag_GetRequest_PDU) {
		data.cut(-1);
		length = m_GetRequest_PDU->decode(data);
	}
	return length;
}

int GetRequest_PDU::encode(DataBlock& data)
{
	int length = -1;
	DataBlock contents;
	u_int8_t tag = tag_GetRequest_PDU;
	length = m_GetRequest_PDU->encode(contents);
	DataBlock len = ASNLib::buildLength(contents);
	data.append(&tag,1);
	data.append(len);
	data.append(contents);
	return length;
}

void GetRequest_PDU::getParams(NamedList* params)
{}

void GetRequest_PDU::setParams(NamedList* params)
{}

/**
  * GetNextRequest_PDU
  */
GetNextRequest_PDU::GetNextRequest_PDU()
{
	m_GetNextRequest_PDU = new PDU();
}

GetNextRequest_PDU::GetNextRequest_PDU(void* data, int len)
{
	m_GetNextRequest_PDU = new PDU();
	DataBlock db(data,len);
	decode(db);
}

GetNextRequest_PDU::~GetNextRequest_PDU()
{
	TelEngine::destruct(m_GetNextRequest_PDU);
}

int GetNextRequest_PDU::decode(DataBlock& data)
{
	int length = 0;
	length = -1;
	if (data.length() < 2)
		return ASNLib::InvalidLengthOrTag;
	if (data[0] == tag_GetNextRequest_PDU) {
		data.cut(-1);
		length = m_GetNextRequest_PDU->decode(data);
	}
	return length;
}

int GetNextRequest_PDU::encode(DataBlock& data)
{
	int length = -1;
	DataBlock contents;
	u_int8_t tag = tag_GetNextRequest_PDU;
	length = m_GetNextRequest_PDU->encode(contents);
	DataBlock len = ASNLib::buildLength(contents);
	data.append(&tag,1);
	data.append(len);
	data.append(contents);
	return length;
}

void GetNextRequest_PDU::getParams(NamedList* params)
{}

void GetNextRequest_PDU::setParams(NamedList* params)
{}

/**
  * Response_PDU
  */
Response_PDU::Response_PDU()
{
	m_Response_PDU = new PDU();
}

Response_PDU::Response_PDU(void* data, int len)
{
	m_Response_PDU = new PDU();
	DataBlock db(data,len);
	decode(db);
}

Response_PDU::~Response_PDU()
{
	TelEngine::destruct(m_Response_PDU);
}

int Response_PDU::decode(DataBlock& data)
{
	int length = 0;
	length = -1;
	if (data.length() < 2)
		return ASNLib::InvalidLengthOrTag;
	if (data[0] == tag_Response_PDU) {
		data.cut(-1);
		length = m_Response_PDU->decode(data);
	}
	return length;
}

int Response_PDU::encode(DataBlock& data)
{
	int length = -1;
	DataBlock contents;
	u_int8_t tag = tag_Response_PDU;
	length = m_Response_PDU->encode(contents);
	DataBlock len = ASNLib::buildLength(contents);
	data.append(&tag,1);
	data.append(len);
	data.append(contents);
	return length;
}

void Response_PDU::getParams(NamedList* params)
{}

void Response_PDU::setParams(NamedList* params)
{}

/**
  * SetRequest_PDU
  */
SetRequest_PDU::SetRequest_PDU()
{
	m_SetRequest_PDU = new PDU();
}

SetRequest_PDU::SetRequest_PDU(void* data, int len)
{
	m_SetRequest_PDU = new PDU();
	DataBlock db(data,len);
	decode(db);
}

SetRequest_PDU::~SetRequest_PDU()
{
	TelEngine::destruct(m_SetRequest_PDU);
}

int SetRequest_PDU::decode(DataBlock& data)
{
	int length = 0;
	length = -1;
	if (data.length() < 2)
		return ASNLib::InvalidLengthOrTag;
	if (data[0] == tag_SetRequest_PDU) {
		data.cut(-1);
		length = m_SetRequest_PDU->decode(data);
	}
	return length;
}

int SetRequest_PDU::encode(DataBlock& data)
{
	int length = -1;
	DataBlock contents;
	u_int8_t tag = tag_SetRequest_PDU;
	length = m_SetRequest_PDU->encode(contents);
	DataBlock len = ASNLib::buildLength(contents);
	data.append(&tag,1);
	data.append(len);
	data.append(contents);
	return length;
}

void SetRequest_PDU::getParams(NamedList* params)
{}

void SetRequest_PDU::setParams(NamedList* params)
{}

/**
  * GetBulkRequest_PDU
  */
GetBulkRequest_PDU::GetBulkRequest_PDU()
{
	m_GetBulkRequest_PDU = new BulkPDU();
}

GetBulkRequest_PDU::GetBulkRequest_PDU(void* data, int len)
{
	m_GetBulkRequest_PDU = new BulkPDU();
	DataBlock db(data,len);
	decode(db);
}

GetBulkRequest_PDU::~GetBulkRequest_PDU()
{
	TelEngine::destruct(m_GetBulkRequest_PDU);
}

int GetBulkRequest_PDU::decode(DataBlock& data)
{
	int length = 0;
	length = -1;
	if (data.length() < 2)
		return ASNLib::InvalidLengthOrTag;
	if (data[0] == tag_GetBulkRequest_PDU) {
		data.cut(-1);
		length = m_GetBulkRequest_PDU->decode(data);
	}
	return length;
}

int GetBulkRequest_PDU::encode(DataBlock& data)
{
	int length = -1;
	DataBlock contents;
	u_int8_t tag = tag_GetBulkRequest_PDU;
	length = m_GetBulkRequest_PDU->encode(contents);
	DataBlock len = ASNLib::buildLength(contents);
	data.append(&tag,1);
	data.append(len);
	data.append(contents);
	return length;
}

void GetBulkRequest_PDU::getParams(NamedList* params)
{}

void GetBulkRequest_PDU::setParams(NamedList* params)
{}

/**
  * InformRequest_PDU
  */
InformRequest_PDU::InformRequest_PDU()
{
	m_InformRequest_PDU = new PDU();
}

InformRequest_PDU::InformRequest_PDU(void* data, int len)
{
	m_InformRequest_PDU = new PDU();
	DataBlock db(data,len);
	decode(db);
}

InformRequest_PDU::~InformRequest_PDU()
{
	TelEngine::destruct(m_InformRequest_PDU);
}

int InformRequest_PDU::decode(DataBlock& data)
{
	int length = 0;
	length = -1;
	if (data.length() < 2)
		return ASNLib::InvalidLengthOrTag;
	if (data[0] == tag_InformRequest_PDU) {
		data.cut(-1);
		length = m_InformRequest_PDU->decode(data);
	}
	return length;
}

int InformRequest_PDU::encode(DataBlock& data)
{
	int length = -1;
	DataBlock contents;
	u_int8_t tag = tag_InformRequest_PDU;
	length = m_InformRequest_PDU->encode(contents);
	DataBlock len = ASNLib::buildLength(contents);
	data.append(&tag,1);
	data.append(len);
	data.append(contents);
	return length;
}

void InformRequest_PDU::getParams(NamedList* params)
{}

void InformRequest_PDU::setParams(NamedList* params)
{}

/**
  * SNMPv2_Trap_PDU
  */
SNMPv2_Trap_PDU::SNMPv2_Trap_PDU()
{
	m_SNMPv2_Trap_PDU = new PDU();
}

SNMPv2_Trap_PDU::SNMPv2_Trap_PDU(void* data, int len)
{
	m_SNMPv2_Trap_PDU = new PDU();
	DataBlock db(data,len);
	decode(db);
}

SNMPv2_Trap_PDU::~SNMPv2_Trap_PDU()
{
	TelEngine::destruct(m_SNMPv2_Trap_PDU);
}

int SNMPv2_Trap_PDU::decode(DataBlock& data)
{
	int length = 0;
	length = -1;
	if (data.length() < 2)
		return ASNLib::InvalidLengthOrTag;
	if (data[0] == tag_SNMPv2_Trap_PDU) {
		data.cut(-1);
		length = m_SNMPv2_Trap_PDU->decode(data);
	}
	return length;
}

int SNMPv2_Trap_PDU::encode(DataBlock& data)
{
	int length = -1;
	DataBlock contents;
	u_int8_t tag = tag_SNMPv2_Trap_PDU;
	length = m_SNMPv2_Trap_PDU->encode(contents);
	DataBlock len = ASNLib::buildLength(contents);
	data.append(&tag,1);
	data.append(len);
	data.append(contents);
	return length;
}

void SNMPv2_Trap_PDU::getParams(NamedList* params)
{}

void SNMPv2_Trap_PDU::setParams(NamedList* params)
{}

/**
  * Report_PDU
  */
Report_PDU::Report_PDU()
{
	m_Report_PDU = new PDU();
}

Report_PDU::Report_PDU(void* data, int len)
{
	m_Report_PDU = new PDU();
	DataBlock db(data,len);
	decode(db);
}

Report_PDU::~Report_PDU()
{
	TelEngine::destruct(m_Report_PDU);
}

int Report_PDU::decode(DataBlock& data)
{
	int length = 0;
	length = -1;
	if (data.length() < 2)
		return ASNLib::InvalidLengthOrTag;
	if (data[0] == tag_Report_PDU) {
		data.cut(-1);
		length = m_Report_PDU->decode(data);
	}
	return length;
}

int Report_PDU::encode(DataBlock& data)
{
	int length = -1;
	DataBlock contents;
	u_int8_t tag = tag_Report_PDU;
	length = m_Report_PDU->encode(contents);
	DataBlock len = ASNLib::buildLength(contents);
	data.append(&tag,1);
	data.append(len);
	data.append(contents);
	return length;
}

void Report_PDU::getParams(NamedList* params)
{}

void Report_PDU::setParams(NamedList* params)
{}

/**
  * PDU
  */
PDU::PDU()
{
	m_variable_bindings = new VarBindList();
}

PDU::PDU(void* data, int len)
{
	m_variable_bindings = new VarBindList();
	DataBlock db(data,len);
	decode(db);
}

PDU::~PDU()
{
	TelEngine::destruct(m_variable_bindings);
}

int PDU::decode(DataBlock& data)
{
	int length = 0;
	length = ASNLib::decodeSequence(data,false);
	if (length < 0)
		return length;
	length = ASNLib::decodeINT32(data,&m_request_id,true);
	if (m_request_id < s_request_idMinSize)
		DDebug(DebugAll,"Constraint break error");
	if (s_request_idMaxSize < m_request_id)
		DDebug(DebugAll,"Constraint break error");
	length = ASNLib::decodeINT32(data,&m_error_status,true);
	if ( m_error_status < s_noError_error_status || m_error_status > s_inconsistentName_error_status)
		return ASNLib::InvalidContentsError;
	length = ASNLib::decodeINT32(data,&m_error_index,true);
	if (m_error_index < s_error_indexMinSize)
		DDebug(DebugAll,"Constraint break error");
	if (s_error_indexMaxSize < m_error_index)
		DDebug(DebugAll,"Constraint break error");
	length = m_variable_bindings->decode(data);
	return length;
}

int PDU::encode(DataBlock& data)
{
	int length = -1;
	DataBlock seqDb;
	DataBlock db;
	length = 0;
	db =  ASNLib::encodeInteger(m_request_id,true);
	seqDb.append(db);
	db =  ASNLib::encodeInteger(m_error_status,true);
	seqDb.append(db);
	db =  ASNLib::encodeInteger(m_error_index,true);
	seqDb.append(db);
	m_variable_bindings->encode(seqDb);
	length = ASNLib::encodeSequence(seqDb,false);
	data.append(seqDb);
	return length;
}

void PDU::getParams(NamedList* params)
{}

void PDU::setParams(NamedList* params)
{}

/**
  * BulkPDU
  */
BulkPDU::BulkPDU()
{
	m_variable_bindings = new VarBindList();
}

BulkPDU::BulkPDU(void* data, int len)
{
	m_variable_bindings = new VarBindList();
	DataBlock db(data,len);
	decode(db);
}

BulkPDU::~BulkPDU()
{
	TelEngine::destruct(m_variable_bindings);
}

int BulkPDU::decode(DataBlock& data)
{
	int length = 0;
	length = ASNLib::decodeSequence(data,false);
	if (length < 0)
		return length;
	length = ASNLib::decodeINT32(data,&m_request_id,true);
	if (m_request_id < s_request_idMinSize)
		DDebug(DebugAll,"Constraint break error");
	if (s_request_idMaxSize < m_request_id)
		DDebug(DebugAll,"Constraint break error");
	length = ASNLib::decodeINT32(data,&m_non_repeaters,true);
	if (m_non_repeaters < s_non_repeatersMinSize)
		DDebug(DebugAll,"Constraint break error");
	if (s_non_repeatersMaxSize < m_non_repeaters)
		DDebug(DebugAll,"Constraint break error");
	length = ASNLib::decodeINT32(data,&m_max_repetitions,true);
	if (m_max_repetitions < s_max_repetitionsMinSize)
		DDebug(DebugAll,"Constraint break error");
	if (s_max_repetitionsMaxSize < m_max_repetitions)
		DDebug(DebugAll,"Constraint break error");
	length = m_variable_bindings->decode(data);
	return length;
}

int BulkPDU::encode(DataBlock& data)
{
	int length = -1;
	DataBlock seqDb;
	DataBlock db;
	length = 0;
	db =  ASNLib::encodeInteger(m_request_id,true);
	seqDb.append(db);
	db =  ASNLib::encodeInteger(m_non_repeaters,true);
	seqDb.append(db);
	db =  ASNLib::encodeInteger(m_max_repetitions,true);
	seqDb.append(db);
	m_variable_bindings->encode(seqDb);
	length = ASNLib::encodeSequence(seqDb,false);
	data.append(seqDb);
	return length;
}

void BulkPDU::getParams(NamedList* params)
{}

void BulkPDU::setParams(NamedList* params)
{}

/**
  * VarBind
  */
VarBind::VarBind()
{
	m_name = new ObjectName();
	m_value = new ObjectSyntax();
}

VarBind::VarBind(void* data, int len)
{
	m_name = new ObjectName();
	m_value = new ObjectSyntax();
	DataBlock db(data,len);
	decode(db);
}

VarBind::~VarBind()
{
	TelEngine::destruct(m_name);
	TelEngine::destruct(m_value);
}

int VarBind::decode(DataBlock& data)
{
	int length = 0;
	length = ASNLib::decodeSequence(data,true);
	if (length < 0)
		return length;
	length = m_name->decode(data);
	length = m_value->decode(data);
	if (length >= 0) {
		m_choiceType = VALUE;
		return length;
	}
	length = ASNLib::decodeNull(data,true);
	if (length >= 0) {
		m_choiceType = UNSPECIFIED;
		return length;
	}
	length = -1;
	if (data.length() < 2)
		return ASNLib::InvalidLengthOrTag;
	if (data[0] == tag_noSuchObject) {
		data.cut(-1);
		length = ASNLib::decodeNull(data,false);
	}
	if (length >= 0) {
		m_choiceType = NOSUCHOBJECT;
		return length;
	}
	length = -1;
	if (data.length() < 2)
		return ASNLib::InvalidLengthOrTag;
	if (data[0] == tag_noSuchInstance) {
		data.cut(-1);
		length = ASNLib::decodeNull(data,false);
	}
	if (length >= 0) {
		m_choiceType = NOSUCHINSTANCE;
		return length;
	}
	length = -1;
	if (data.length() < 2)
		return ASNLib::InvalidLengthOrTag;
	if (data[0] == tag_endOfMibView) {
		data.cut(-1);
		length = ASNLib::decodeNull(data,false);
	}
	if (length >= 0) {
		m_choiceType = ENDOFMIBVIEW;
		return length;
	}
	return length;
}

int VarBind::encode(DataBlock& data)
{
	int length = -1;
	DataBlock seqDb;
	DataBlock db;
	length = 0;
	m_name->encode(seqDb);
		if (m_choiceType == VALUE) {
		length = m_value->encode(seqDb);
	}
	if (m_choiceType == UNSPECIFIED) {
		DataBlock db = ASNLib::encodeNull(true);
		seqDb.append(db);
		length += seqDb.length();
	}
	if (m_choiceType == NOSUCHOBJECT) {
		DataBlock contents;
		u_int8_t tag = tag_noSuchObject;
		DataBlock db = ASNLib::encodeNull(false);
		contents.append(db);
		length += contents.length();
		DataBlock len = ASNLib::buildLength(contents);
		seqDb.append(&tag,1);
		seqDb.append(len);
		seqDb.append(contents);
	}
	if (m_choiceType == NOSUCHINSTANCE) {
		DataBlock contents;
		u_int8_t tag = tag_noSuchInstance;
		DataBlock db = ASNLib::encodeNull(false);
		contents.append(db);
		length += contents.length();
		DataBlock len = ASNLib::buildLength(contents);
		seqDb.append(&tag,1);
		seqDb.append(len);
		seqDb.append(contents);
	}
	if (m_choiceType == ENDOFMIBVIEW) {
		DataBlock contents;
		u_int8_t tag = tag_endOfMibView;
		DataBlock db = ASNLib::encodeNull(false);
		contents.append(db);
		length += contents.length();
		DataBlock len = ASNLib::buildLength(contents);
		seqDb.append(&tag,1);
		seqDb.append(len);
		seqDb.append(contents);
	}

	seqDb.append(db);
	length = ASNLib::encodeSequence(seqDb,true);
	data.append(seqDb);
	return length;
}

void VarBind::getParams(NamedList* params)
{}

void VarBind::setParams(NamedList* params)
{}

/**
  * VarBindList
  */
VarBindList::VarBindList()
{
}

VarBindList::VarBindList(void* data, int len)
{
	DataBlock db(data,len);
	decode(db);
}

VarBindList::~VarBindList()
{
}

int VarBindList::decode(DataBlock& data)
{
	int length = 0;
	length = 0;
	length = ASNLib::decodeSequence(data,true);
	if (length < 0)
		return length;
	int l = 0;
	while (l > -1) {
		VarBind* obj = new VarBind();
		l = obj->decode(data);
		if (l > -1) {
			length += l;
			m_list.append(obj);
		}
		else
			TelEngine::destruct(obj);
	}
	return length;
}

int VarBindList::encode(DataBlock& data)
{
	int length = -1;
	DataBlock db;
	length = 0;
	for (unsigned int i = 0; i < m_list.count(); i++) {
		VarBind* obj = static_cast<VarBind*>(m_list[i]);
		int l = obj->encode(db);
		if (l <= -1)
		return -1;
		length += l;
	}
	length = ASNLib::encodeSequence(db,true);
	if (length < 0)
		return length;
	data.append(db);
	return length;
}

void VarBindList::getParams(NamedList* params)
{}

void VarBindList::setParams(NamedList* params)
{}

/**
  * DisplayString
  */
DisplayString::DisplayString()
{
}

DisplayString::DisplayString(void* data, int len)
{
	DataBlock db(data,len);
	decode(db);
}

DisplayString::~DisplayString()
{
}

int DisplayString::decode(DataBlock& data)
{
	int length = 0;
	length = ASNLib::decodeOctetString(data,&m_DisplayString,true);
	if (length < s_DisplayStringSizeMinSize)
		DDebug(DebugAll,"Constraint break error");
	if (s_DisplayStringSizeMaxSize < length)
		DDebug(DebugAll,"Constraint break error");
	return length;
}

int DisplayString::encode(DataBlock& data)
{
	int length = -1;
	const DataBlock db = ASNLib::encodeOctetString(m_DisplayString,true);
	data.append(db);
	length = db.length();
	return length;
}

void DisplayString::getParams(NamedList* params)
{}

void DisplayString::setParams(NamedList* params)
{}

/**
  * PhysAddress
  */
PhysAddress::PhysAddress()
{
}

PhysAddress::PhysAddress(void* data, int len)
{
	DataBlock db(data,len);
	decode(db);
}

PhysAddress::~PhysAddress()
{
}

int PhysAddress::decode(DataBlock& data)
{
	int length = 0;
	length = ASNLib::decodeOctetString(data,&m_PhysAddress,true);
	return length;
}

int PhysAddress::encode(DataBlock& data)
{
	int length = -1;
	const DataBlock db = ASNLib::encodeOctetString(m_PhysAddress,true);
	data.append(db);
	length = db.length();
	return length;
}

void PhysAddress::getParams(NamedList* params)
{}

void PhysAddress::setParams(NamedList* params)
{}

/**
  * MacAddress
  */
MacAddress::MacAddress()
{
}

MacAddress::MacAddress(void* data, int len)
{
	DataBlock db(data,len);
	decode(db);
}

MacAddress::~MacAddress()
{
}

int MacAddress::decode(DataBlock& data)
{
	int length = 0;
	length = ASNLib::decodeOctetString(data,&m_MacAddress,true);
	if (length != s_MacAddressSize)
		DDebug(DebugAll,"Constraint break error");
	return length;
}

int MacAddress::encode(DataBlock& data)
{
	int length = -1;
	const DataBlock db = ASNLib::encodeOctetString(m_MacAddress,true);
	data.append(db);
	length = db.length();
	return length;
}

void MacAddress::getParams(NamedList* params)
{}

void MacAddress::setParams(NamedList* params)
{}

/**
  * TruthValue
  */
TruthValue::TruthValue()
{
}

TruthValue::TruthValue(void* data, int len)
{
	DataBlock db(data,len);
	decode(db);
}

TruthValue::~TruthValue()
{
}

int TruthValue::decode(DataBlock& data)
{
	int length = 0;
	length = ASNLib::decodeINT32(data,&m_TruthValue,true);
	if ( m_TruthValue < s_true_TruthValue || m_TruthValue > s_false_TruthValue)
		return ASNLib::InvalidContentsError;
	return length;
}

int TruthValue::encode(DataBlock& data)
{
	int length = -1;
	const DataBlock db = ASNLib::encodeInteger(m_TruthValue,true);
	data.append(db);
	length = db.length();
	return length;
}

void TruthValue::getParams(NamedList* params)
{}

void TruthValue::setParams(NamedList* params)
{}

/**
  * TestAndIncr
  */
TestAndIncr::TestAndIncr()
{
}

TestAndIncr::TestAndIncr(void* data, int len)
{
	DataBlock db(data,len);
	decode(db);
}

TestAndIncr::~TestAndIncr()
{
}

int TestAndIncr::decode(DataBlock& data)
{
	int length = 0;
	length = ASNLib::decodeUINT32(data,&m_TestAndIncr,true);
	if (m_TestAndIncr < s_TestAndIncrMinSize)
		DDebug(DebugAll,"Constraint break error");
	if (s_TestAndIncrMaxSize < m_TestAndIncr)
		DDebug(DebugAll,"Constraint break error");
	return length;
}

int TestAndIncr::encode(DataBlock& data)
{
	int length = -1;
	const DataBlock db = ASNLib::encodeInteger(m_TestAndIncr,true);
	data.append(db);
	length = db.length();
	return length;
}

void TestAndIncr::getParams(NamedList* params)
{}

void TestAndIncr::setParams(NamedList* params)
{}

/**
  * AutonomousType
  */
AutonomousType::AutonomousType()
{
}

AutonomousType::AutonomousType(void* data, int len)
{
	DataBlock db(data,len);
	decode(db);
}

AutonomousType::~AutonomousType()
{
}

int AutonomousType::decode(DataBlock& data)
{
	int length = 0;
	length = ASNLib::decodeOID(data,&m_AutonomousType,true);
	return length;
}

int AutonomousType::encode(DataBlock& data)
{
	int length = -1;
	const DataBlock db = ASNLib::encodeOID(m_AutonomousType,true);
	data.append(db);
	length = db.length();
	return length;
}

void AutonomousType::getParams(NamedList* params)
{}

void AutonomousType::setParams(NamedList* params)
{}

/**
  * InstancePointer
  */
InstancePointer::InstancePointer()
{
}

InstancePointer::InstancePointer(void* data, int len)
{
	DataBlock db(data,len);
	decode(db);
}

InstancePointer::~InstancePointer()
{
}

int InstancePointer::decode(DataBlock& data)
{
	int length = 0;
	length = ASNLib::decodeOID(data,&m_InstancePointer,true);
	return length;
}

int InstancePointer::encode(DataBlock& data)
{
	int length = -1;
	const DataBlock db = ASNLib::encodeOID(m_InstancePointer,true);
	data.append(db);
	length = db.length();
	return length;
}

void InstancePointer::getParams(NamedList* params)
{}

void InstancePointer::setParams(NamedList* params)
{}

/**
  * VariablePointer
  */
VariablePointer::VariablePointer()
{
}

VariablePointer::VariablePointer(void* data, int len)
{
	DataBlock db(data,len);
	decode(db);
}

VariablePointer::~VariablePointer()
{
}

int VariablePointer::decode(DataBlock& data)
{
	int length = 0;
	length = ASNLib::decodeOID(data,&m_VariablePointer,true);
	return length;
}

int VariablePointer::encode(DataBlock& data)
{
	int length = -1;
	const DataBlock db = ASNLib::encodeOID(m_VariablePointer,true);
	data.append(db);
	length = db.length();
	return length;
}

void VariablePointer::getParams(NamedList* params)
{}

void VariablePointer::setParams(NamedList* params)
{}

/**
  * RowPointer
  */
RowPointer::RowPointer()
{
}

RowPointer::RowPointer(void* data, int len)
{
	DataBlock db(data,len);
	decode(db);
}

RowPointer::~RowPointer()
{
}

int RowPointer::decode(DataBlock& data)
{
	int length = 0;
	length = ASNLib::decodeOID(data,&m_RowPointer,true);
	return length;
}

int RowPointer::encode(DataBlock& data)
{
	int length = -1;
	const DataBlock db = ASNLib::encodeOID(m_RowPointer,true);
	data.append(db);
	length = db.length();
	return length;
}

void RowPointer::getParams(NamedList* params)
{}

void RowPointer::setParams(NamedList* params)
{}

/**
  * RowStatus
  */
RowStatus::RowStatus()
{
}

RowStatus::RowStatus(void* data, int len)
{
	DataBlock db(data,len);
	decode(db);
}

RowStatus::~RowStatus()
{
}

int RowStatus::decode(DataBlock& data)
{
	int length = 0;
	length = ASNLib::decodeINT32(data,&m_RowStatus,true);
	if ( m_RowStatus < s_active_RowStatus || m_RowStatus > s_destroy_RowStatus)
		return ASNLib::InvalidContentsError;
	return length;
}

int RowStatus::encode(DataBlock& data)
{
	int length = -1;
	const DataBlock db = ASNLib::encodeInteger(m_RowStatus,true);
	data.append(db);
	length = db.length();
	return length;
}

void RowStatus::getParams(NamedList* params)
{}

void RowStatus::setParams(NamedList* params)
{}

/**
  * TimeStamp
  */
TimeStamp::TimeStamp()
{
}

TimeStamp::TimeStamp(void* data, int len)
{
	DataBlock db(data,len);
	decode(db);
}

TimeStamp::~TimeStamp()
{
}

int TimeStamp::decode(DataBlock& data)
{
	int length = 0;
	length = m_TimeStamp->decode(data);
	return length;
}

int TimeStamp::encode(DataBlock& data)
{
	int length = -1;
	length = m_TimeStamp->encode(data);
	return length;
}

void TimeStamp::getParams(NamedList* params)
{}

void TimeStamp::setParams(NamedList* params)
{}

/**
  * TimeInterval
  */
TimeInterval::TimeInterval()
{
}

TimeInterval::TimeInterval(void* data, int len)
{
	DataBlock db(data,len);
	decode(db);
}

TimeInterval::~TimeInterval()
{
}

int TimeInterval::decode(DataBlock& data)
{
	int length = 0;
	length = ASNLib::decodeUINT32(data,&m_TimeInterval,true);
	if (m_TimeInterval < s_TimeIntervalMinSize)
		DDebug(DebugAll,"Constraint break error");
	if (s_TimeIntervalMaxSize < m_TimeInterval)
		DDebug(DebugAll,"Constraint break error");
	return length;
}

int TimeInterval::encode(DataBlock& data)
{
	int length = -1;
	const DataBlock db = ASNLib::encodeInteger(m_TimeInterval,true);
	data.append(db);
	length = db.length();
	return length;
}

void TimeInterval::getParams(NamedList* params)
{}

void TimeInterval::setParams(NamedList* params)
{}

/**
  * DateAndTime
  */
DateAndTime::DateAndTime()
{
}

DateAndTime::DateAndTime(void* data, int len)
{
	DataBlock db(data,len);
	decode(db);
}

DateAndTime::~DateAndTime()
{
}

int DateAndTime::decode(DataBlock& data)
{
	int length = 0;
	length = ASNLib::decodeOctetString(data,&m_DateAndTime,true);
	if (length != s_DateAndTimeSize_0)
		DDebug(DebugAll,"Constraint break error");
	if (length != s_DateAndTimeSize_1)
		DDebug(DebugAll,"Constraint break error");
	DDebug(DebugAll,"Constraint break error");
	return length;
}

int DateAndTime::encode(DataBlock& data)
{
	int length = -1;
	const DataBlock db = ASNLib::encodeOctetString(m_DateAndTime,true);
	data.append(db);
	length = db.length();
	return length;
}

void DateAndTime::getParams(NamedList* params)
{}

void DateAndTime::setParams(NamedList* params)
{}

/**
  * StorageType
  */
StorageType::StorageType()
{
}

StorageType::StorageType(void* data, int len)
{
	DataBlock db(data,len);
	decode(db);
}

StorageType::~StorageType()
{
}

int StorageType::decode(DataBlock& data)
{
	int length = 0;
	length = ASNLib::decodeINT32(data,&m_StorageType,true);
	if ( m_StorageType < s_other_StorageType || m_StorageType > s_readOnly_StorageType)
		return ASNLib::InvalidContentsError;
	return length;
}

int StorageType::encode(DataBlock& data)
{
	int length = -1;
	const DataBlock db = ASNLib::encodeInteger(m_StorageType,true);
	data.append(db);
	length = db.length();
	return length;
}

void StorageType::getParams(NamedList* params)
{}

void StorageType::setParams(NamedList* params)
{}

/**
  * TDomain
  */
TDomain::TDomain()
{
}

TDomain::TDomain(void* data, int len)
{
	DataBlock db(data,len);
	decode(db);
}

TDomain::~TDomain()
{
}

int TDomain::decode(DataBlock& data)
{
	int length = 0;
	length = ASNLib::decodeOID(data,&m_TDomain,true);
	return length;
}

int TDomain::encode(DataBlock& data)
{
	int length = -1;
	const DataBlock db = ASNLib::encodeOID(m_TDomain,true);
	data.append(db);
	length = db.length();
	return length;
}

void TDomain::getParams(NamedList* params)
{}

void TDomain::setParams(NamedList* params)
{}

/**
  * TAddress
  */
TAddress::TAddress()
{
}

TAddress::TAddress(void* data, int len)
{
	DataBlock db(data,len);
	decode(db);
}

TAddress::~TAddress()
{
}

int TAddress::decode(DataBlock& data)
{
	int length = 0;
	length = ASNLib::decodeOctetString(data,&m_TAddress,true);
	if (length < s_TAddressSizeMinSize)
		DDebug(DebugAll,"Constraint break error");
	if (s_TAddressSizeMaxSize < length)
		DDebug(DebugAll,"Constraint break error");
	return length;
}

int TAddress::encode(DataBlock& data)
{
	int length = -1;
	const DataBlock db = ASNLib::encodeOctetString(m_TAddress,true);
	data.append(db);
	length = db.length();
	return length;
}

void TAddress::getParams(NamedList* params)
{}

void TAddress::setParams(NamedList* params)
{}

/**
  * SNMPv3Message
  */
SNMPv3Message::SNMPv3Message()
{
	m_msgGlobalData = new HeaderData();
	m_msgData = new ScopedPduData();
}

SNMPv3Message::SNMPv3Message(void* data, int len)
{
	m_msgGlobalData = new HeaderData();
	m_msgData = new ScopedPduData();
	DataBlock db(data,len);
	decode(db);
}

SNMPv3Message::~SNMPv3Message()
{
	TelEngine::destruct(m_msgGlobalData);
	TelEngine::destruct(m_msgData);
}

int SNMPv3Message::decode(DataBlock& data)
{
	int length = 0;
	length = ASNLib::decodeSequence(data,true);
	if (length < 0)
		return length;
	length = ASNLib::decodeUINT32(data,&m_msgVersion,true);
	if (m_msgVersion < s_msgVersionMinSize)
		DDebug(DebugAll,"Constraint break error");
	if (s_msgVersionMaxSize < m_msgVersion)
		DDebug(DebugAll,"Constraint break error");
	length = m_msgGlobalData->decode(data);
	length = ASNLib::decodeOctetString(data,&m_msgSecurityParameters,true);
	length = m_msgData->decode(data);
	return length;
}

int SNMPv3Message::encode(DataBlock& data)
{
	int length = -1;
	DataBlock seqDb;
	DataBlock db;
	length = 0;
	db =  ASNLib::encodeInteger(m_msgVersion,true);
	seqDb.append(db);
	m_msgGlobalData->encode(seqDb);
	db = ASNLib::encodeOctetString(m_msgSecurityParameters,true);
	seqDb.append(db);
	m_msgData->encode(seqDb);
	length = ASNLib::encodeSequence(seqDb,true);
	data.append(seqDb);
	return length;
}

void SNMPv3Message::getParams(NamedList* params)
{}

void SNMPv3Message::setParams(NamedList* params)
{}

/**
  * HeaderData
  */
HeaderData::HeaderData()
{
}

HeaderData::HeaderData(void* data, int len)
{
	DataBlock db(data,len);
	decode(db);
}

HeaderData::~HeaderData()
{
}

int HeaderData::decode(DataBlock& data)
{
	int length = 0;
	length = ASNLib::decodeSequence(data,true);
	if (length < 0)
		return length;
	length = ASNLib::decodeUINT32(data,&m_msgID,true);
	if (m_msgID < s_msgIDMinSize)
		DDebug(DebugAll,"Constraint break error");
	if (s_msgIDMaxSize < m_msgID)
		DDebug(DebugAll,"Constraint break error");
	length = ASNLib::decodeUINT32(data,&m_msgMaxSize,true);
	if (m_msgMaxSize < s_msgMaxSizeMinSize)
		DDebug(DebugAll,"Constraint break error");
	if (s_msgMaxSizeMaxSize < m_msgMaxSize)
		DDebug(DebugAll,"Constraint break error");
	length = ASNLib::decodeOctetString(data,&m_msgFlags,true);
	if (length != s_msgFlagsSize)
		DDebug(DebugAll,"Constraint break error");
	length = ASNLib::decodeUINT32(data,&m_msgSecurityModel,true);
	if (m_msgSecurityModel < s_msgSecurityModelMinSize)
		DDebug(DebugAll,"Constraint break error");
	if (s_msgSecurityModelMaxSize < m_msgSecurityModel)
		DDebug(DebugAll,"Constraint break error");
	return length;
}

int HeaderData::encode(DataBlock& data)
{
	int length = -1;
	DataBlock seqDb;
	DataBlock db;
	length = 0;
	db =  ASNLib::encodeInteger(m_msgID,true);
	seqDb.append(db);
	db =  ASNLib::encodeInteger(m_msgMaxSize,true);
	seqDb.append(db);
	db = ASNLib::encodeOctetString(m_msgFlags,true);
	seqDb.append(db);
	db =  ASNLib::encodeInteger(m_msgSecurityModel,true);
	seqDb.append(db);
	length = ASNLib::encodeSequence(seqDb,true);
	data.append(seqDb);
	return length;
}

void HeaderData::getParams(NamedList* params)
{}

void HeaderData::setParams(NamedList* params)
{}

/**
  * ScopedPduData
  */
ScopedPduData::ScopedPduData()
{
	m_plaintext = new ScopedPDU();
}

ScopedPduData::ScopedPduData(void* data, int len)
{
	m_plaintext = new ScopedPDU();
	DataBlock db(data,len);
	decode(db);
}

ScopedPduData::~ScopedPduData()
{
	TelEngine::destruct(m_plaintext);
}

int ScopedPduData::decode(DataBlock& data)
{
	int length = 0;
	length = m_plaintext->decode(data);
	if (length >= 0) {
		m_choiceType = PLAINTEXT;
		return length;
	}
	length = ASNLib::decodeOctetString(data,&m_encryptedPDU,true);
	if (length >= 0) {
		m_choiceType = ENCRYPTEDPDU;
		return length;
	}
	return length;
}

int ScopedPduData::encode(DataBlock& data)
{
	int length = -1;
	if (m_choiceType == PLAINTEXT) {
		length = m_plaintext->encode(data);
	}
	if (m_choiceType == ENCRYPTEDPDU) {
		const DataBlock db = ASNLib::encodeOctetString(m_encryptedPDU,true);
		data.append(db);
		length = db.length();
	}
	return length;
}

void ScopedPduData::getParams(NamedList* params)
{}

void ScopedPduData::setParams(NamedList* params)
{}

/**
  * ScopedPDU
  */
ScopedPDU::ScopedPDU()
{
}

ScopedPDU::ScopedPDU(void* data, int len)
{
	DataBlock db(data,len);
	decode(db);
}

ScopedPDU::~ScopedPDU()
{
}

int ScopedPDU::decode(DataBlock& data)
{
	int length = 0;
	length = ASNLib::decodeSequence(data,true);
	if (length < 0)
		return length;
	length = ASNLib::decodeOctetString(data,&m_contextEngineID,true);
	length = ASNLib::decodeOctetString(data,&m_contextName,true);
	length = ASNLib::decodeAny(data,&m_data,true);
	if (length < 0)
		return length;
	return length;
}

int ScopedPDU::encode(DataBlock& data)
{
	int length = -1;
	DataBlock seqDb;
	DataBlock db;
	length = 0;
	db = ASNLib::encodeOctetString(m_contextEngineID,true);
	seqDb.append(db);
	db = ASNLib::encodeOctetString(m_contextName,true);
	seqDb.append(db);
	db = ASNLib::encodeAny(m_data,true);
	seqDb.append(db);
	length = ASNLib::encodeSequence(seqDb,true);
	data.append(seqDb);
	return length;
}

void ScopedPDU::getParams(NamedList* params)
{}

void ScopedPDU::setParams(NamedList* params)
{}

/**
  * Message
  */
Message::Message()
{
}

Message::Message(void* data, int len)
{
	DataBlock db(data,len);
	decode(db);
}

Message::~Message()
{
}

int Message::decode(DataBlock& data)
{
	int length = 0;
	length = ASNLib::decodeSequence(data,true);
	if (length < 0)
		return length;
	length = ASNLib::decodeINT32(data,&m_version,true);
	if ( m_version < s_version_1_version || m_version > s_version_2_version)
		return ASNLib::InvalidContentsError;
	length = ASNLib::decodeOctetString(data,&m_community,true);
	length = ASNLib::decodeAny(data,&m_data,true);
	if (length < 0)
		return length;
	return length;
}

int Message::encode(DataBlock& data)
{
	int length = -1;
	DataBlock seqDb;
	DataBlock db;
	length = 0;
	db =  ASNLib::encodeInteger(m_version,true);
	seqDb.append(db);
	db = ASNLib::encodeOctetString(m_community,true);
	seqDb.append(db);
	db = ASNLib::encodeAny(m_data,true);
	seqDb.append(db);
	length = ASNLib::encodeSequence(seqDb,true);
	data.append(seqDb);
	return length;
}

void Message::getParams(NamedList* params)
{}

void Message::setParams(NamedList* params)
{}

/**
  * KeyChange
  */
KeyChange::KeyChange()
{
}

KeyChange::KeyChange(void* data, int len)
{
	DataBlock db(data,len);
	decode(db);
}

KeyChange::~KeyChange()
{
}

int KeyChange::decode(DataBlock& data)
{
	int length = 0;
	length = ASNLib::decodeOctetString(data,&m_KeyChange,true);
	return length;
}

int KeyChange::encode(DataBlock& data)
{
	int length = -1;
	const DataBlock db = ASNLib::encodeOctetString(m_KeyChange,true);
	data.append(db);
	length = db.length();
	return length;
}

void KeyChange::getParams(NamedList* params)
{}

void KeyChange::setParams(NamedList* params)
{}

/**
  * UsmUserEntry
  */
UsmUserEntry::UsmUserEntry()
{
	m_usmUserEngineID = new SnmpEngineID();
	m_usmUserName = new SnmpAdminString();
	m_usmUserSecurityName = new SnmpAdminString();
	m_usmUserCloneFrom = new RowPointer();
	m_usmUserAuthProtocol = new AutonomousType();
	m_usmUserAuthKeyChange = new KeyChange();
	m_usmUserOwnAuthKeyChange = new KeyChange();
	m_usmUserPrivProtocol = new AutonomousType();
	m_usmUserPrivKeyChange = new KeyChange();
	m_usmUserOwnPrivKeyChange = new KeyChange();
	m_usmUserStorageType = new StorageType();
	m_usmUserStatus = new RowStatus();
}

UsmUserEntry::UsmUserEntry(void* data, int len)
{
	m_usmUserEngineID = new SnmpEngineID();
	m_usmUserName = new SnmpAdminString();
	m_usmUserSecurityName = new SnmpAdminString();
	m_usmUserCloneFrom = new RowPointer();
	m_usmUserAuthProtocol = new AutonomousType();
	m_usmUserAuthKeyChange = new KeyChange();
	m_usmUserOwnAuthKeyChange = new KeyChange();
	m_usmUserPrivProtocol = new AutonomousType();
	m_usmUserPrivKeyChange = new KeyChange();
	m_usmUserOwnPrivKeyChange = new KeyChange();
	m_usmUserStorageType = new StorageType();
	m_usmUserStatus = new RowStatus();

	DataBlock db(data,len);
	decode(db);
}

UsmUserEntry::~UsmUserEntry()
{
	TelEngine::destruct(m_usmUserEngineID);
	TelEngine::destruct(m_usmUserName);
	TelEngine::destruct(m_usmUserSecurityName);
	TelEngine::destruct(m_usmUserCloneFrom);
	TelEngine::destruct(m_usmUserAuthProtocol);
	TelEngine::destruct(m_usmUserAuthKeyChange);
	TelEngine::destruct(m_usmUserOwnAuthKeyChange);
	TelEngine::destruct(m_usmUserPrivProtocol);
	TelEngine::destruct(m_usmUserPrivKeyChange);
	TelEngine::destruct(m_usmUserOwnPrivKeyChange);
	TelEngine::destruct(m_usmUserStorageType);
	TelEngine::destruct(m_usmUserStatus);
}

int UsmUserEntry::decode(DataBlock& data)
{
	int length = 0;
	length = ASNLib::decodeSequence(data,true);
	if (length < 0)
		return length;
	length = m_usmUserEngineID->decode(data);
	length = m_usmUserName->decode(data);
	length = m_usmUserSecurityName->decode(data);
	length = m_usmUserCloneFrom->decode(data);
	length = m_usmUserAuthProtocol->decode(data);
	length = m_usmUserAuthKeyChange->decode(data);
	length = m_usmUserOwnAuthKeyChange->decode(data);
	length = m_usmUserPrivProtocol->decode(data);
	length = m_usmUserPrivKeyChange->decode(data);
	length = m_usmUserOwnPrivKeyChange->decode(data);
	length = ASNLib::decodeOctetString(data,&m_usmUserPublic,true);
	length = m_usmUserStorageType->decode(data);
	length = m_usmUserStatus->decode(data);
	return length;
}

int UsmUserEntry::encode(DataBlock& data)
{
	int length = -1;
	DataBlock seqDb;
	DataBlock db;
	length = 0;
	m_usmUserEngineID->encode(seqDb);
	m_usmUserName->encode(seqDb);
	m_usmUserSecurityName->encode(seqDb);
	m_usmUserCloneFrom->encode(seqDb);
	m_usmUserAuthProtocol->encode(seqDb);
	m_usmUserAuthKeyChange->encode(seqDb);
	m_usmUserOwnAuthKeyChange->encode(seqDb);
	m_usmUserPrivProtocol->encode(seqDb);
	m_usmUserPrivKeyChange->encode(seqDb);
	m_usmUserOwnPrivKeyChange->encode(seqDb);
	db = ASNLib::encodeOctetString(m_usmUserPublic,true);
	seqDb.append(db);
	m_usmUserStorageType->encode(seqDb);
	m_usmUserStatus->encode(seqDb);
	length = ASNLib::encodeSequence(seqDb,true);
	data.append(seqDb);
	return length;
}

void UsmUserEntry::getParams(NamedList* params)
{}

void UsmUserEntry::setParams(NamedList* params)
{}

/**
  * SnmpEngineID
  */
SnmpEngineID::SnmpEngineID()
{
}

SnmpEngineID::SnmpEngineID(void* data, int len)
{
	DataBlock db(data,len);
	decode(db);
}

SnmpEngineID::~SnmpEngineID()
{
}

int SnmpEngineID::decode(DataBlock& data)
{
	int length = 0;
	length = ASNLib::decodeOctetString(data,&m_SnmpEngineID,true);
	if (length < s_SnmpEngineIDSizeMinSize)
		DDebug(DebugAll,"Constraint break error");
	if (s_SnmpEngineIDSizeMaxSize < length)
		DDebug(DebugAll,"Constraint break error");
	return length;
}

int SnmpEngineID::encode(DataBlock& data)
{
	int length = -1;
	const DataBlock db = ASNLib::encodeOctetString(m_SnmpEngineID,true);
	data.append(db);
	length = db.length();
	return length;
}

void SnmpEngineID::getParams(NamedList* params)
{}

void SnmpEngineID::setParams(NamedList* params)
{}

/**
  * SnmpSecurityModel
  */
SnmpSecurityModel::SnmpSecurityModel()
{
}

SnmpSecurityModel::SnmpSecurityModel(void* data, int len)
{
	DataBlock db(data,len);
	decode(db);
}

SnmpSecurityModel::~SnmpSecurityModel()
{
}

int SnmpSecurityModel::decode(DataBlock& data)
{
	int length = 0;
	length = ASNLib::decodeUINT32(data,&m_SnmpSecurityModel,true);
	if (m_SnmpSecurityModel < s_SnmpSecurityModelMinSize)
		DDebug(DebugAll,"Constraint break error");
	if (s_SnmpSecurityModelMaxSize < m_SnmpSecurityModel)
		DDebug(DebugAll,"Constraint break error");
	return length;
}

int SnmpSecurityModel::encode(DataBlock& data)
{
	int length = -1;
	const DataBlock db = ASNLib::encodeInteger(m_SnmpSecurityModel,true);
	data.append(db);
	length = db.length();
	return length;
}

void SnmpSecurityModel::getParams(NamedList* params)
{}

void SnmpSecurityModel::setParams(NamedList* params)
{}

/**
  * SnmpMessageProcessingModel
  */
SnmpMessageProcessingModel::SnmpMessageProcessingModel()
{
}

SnmpMessageProcessingModel::SnmpMessageProcessingModel(void* data, int len)
{
	DataBlock db(data,len);
	decode(db);
}

SnmpMessageProcessingModel::~SnmpMessageProcessingModel()
{
}

int SnmpMessageProcessingModel::decode(DataBlock& data)
{
	int length = 0;
	length = ASNLib::decodeUINT32(data,&m_SnmpMessageProcessingModel,true);
	if (m_SnmpMessageProcessingModel < s_SnmpMessageProcessingModelMinSize)
		DDebug(DebugAll,"Constraint break error");
	if (s_SnmpMessageProcessingModelMaxSize < m_SnmpMessageProcessingModel)
		DDebug(DebugAll,"Constraint break error");
	return length;
}

int SnmpMessageProcessingModel::encode(DataBlock& data)
{
	int length = -1;
	const DataBlock db = ASNLib::encodeInteger(m_SnmpMessageProcessingModel,true);
	data.append(db);
	length = db.length();
	return length;
}

void SnmpMessageProcessingModel::getParams(NamedList* params)
{}

void SnmpMessageProcessingModel::setParams(NamedList* params)
{}

/**
  * SnmpSecurityLevel
  */
SnmpSecurityLevel::SnmpSecurityLevel()
{
}

SnmpSecurityLevel::SnmpSecurityLevel(void* data, int len)
{
	DataBlock db(data,len);
	decode(db);
}

SnmpSecurityLevel::~SnmpSecurityLevel()
{
}

int SnmpSecurityLevel::decode(DataBlock& data)
{
	int length = 0;
	length = ASNLib::decodeINT32(data,&m_SnmpSecurityLevel,true);
	if ( m_SnmpSecurityLevel < s_noAuthNoPriv_SnmpSecurityLevel || m_SnmpSecurityLevel > s_authPriv_SnmpSecurityLevel)
		return ASNLib::InvalidContentsError;
	return length;
}

int SnmpSecurityLevel::encode(DataBlock& data)
{
	int length = -1;
	const DataBlock db = ASNLib::encodeInteger(m_SnmpSecurityLevel,true);
	data.append(db);
	length = db.length();
	return length;
}

void SnmpSecurityLevel::getParams(NamedList* params)
{}

void SnmpSecurityLevel::setParams(NamedList* params)
{}

/**
  * SnmpAdminString
  */
SnmpAdminString::SnmpAdminString()
{
}

SnmpAdminString::SnmpAdminString(void* data, int len)
{
	DataBlock db(data,len);
	decode(db);
}

SnmpAdminString::~SnmpAdminString()
{
}

int SnmpAdminString::decode(DataBlock& data)
{
	int length = 0;
	length = ASNLib::decodeOctetString(data,&m_SnmpAdminString,true);
	if (length < s_SnmpAdminStringSizeMinSize)
		DDebug(DebugAll,"Constraint break error");
	if (s_SnmpAdminStringSizeMaxSize < length)
		DDebug(DebugAll,"Constraint break error");
	return length;
}

int SnmpAdminString::encode(DataBlock& data)
{
	int length = -1;
	const DataBlock db = ASNLib::encodeOctetString(m_SnmpAdminString,true);
	data.append(db);
	length = db.length();
	return length;
}

void SnmpAdminString::getParams(NamedList* params)
{}

void SnmpAdminString::setParams(NamedList* params)
{}

/**
  * UsmSecurityParameters
  */
UsmSecurityParameters::UsmSecurityParameters()
{
}

UsmSecurityParameters::UsmSecurityParameters(void* data, int len)
{
	DataBlock db(data,len);
	decode(db);
}

UsmSecurityParameters::~UsmSecurityParameters()
{
}

int UsmSecurityParameters::decode(DataBlock& data)
{
	int length = 0;
	length = ASNLib::decodeSequence(data,true);
	if (length < 0)
		return length;
	length = ASNLib::decodeOctetString(data,&m_msgAuthoritativeEngineID,true);
	length = ASNLib::decodeUINT32(data,&m_msgAuthoritativeEngineBoots,true);
	if (m_msgAuthoritativeEngineBoots < s_msgAuthoritativeEngineBootsMinSize)
		DDebug(DebugAll,"Constraint break error");
	if (s_msgAuthoritativeEngineBootsMaxSize < m_msgAuthoritativeEngineBoots)
		DDebug(DebugAll,"Constraint break error");
	length = ASNLib::decodeUINT32(data,&m_msgAuthoritativeEngineTime,true);
	if (m_msgAuthoritativeEngineTime < s_msgAuthoritativeEngineTimeMinSize)
		DDebug(DebugAll,"Constraint break error");
	if (s_msgAuthoritativeEngineTimeMaxSize < m_msgAuthoritativeEngineTime)
		DDebug(DebugAll,"Constraint break error");
	length = ASNLib::decodeOctetString(data,&m_msgUserName,true);
	if (length < s_msgUserNameSizeMinSize)
		DDebug(DebugAll,"Constraint break error");
	if (s_msgUserNameSizeMaxSize < length)
		DDebug(DebugAll,"Constraint break error");
	length = ASNLib::decodeOctetString(data,&m_msgAuthenticationParameters,true);
	length = ASNLib::decodeOctetString(data,&m_msgPrivacyParameters,true);
	return length;
}

int UsmSecurityParameters::encode(DataBlock& data)
{
	int length = -1;
	DataBlock seqDb;
	DataBlock db;
	length = 0;
	db = ASNLib::encodeOctetString(m_msgAuthoritativeEngineID,true);
	seqDb.append(db);
	db =  ASNLib::encodeInteger(m_msgAuthoritativeEngineBoots,true);
	seqDb.append(db);
	db =  ASNLib::encodeInteger(m_msgAuthoritativeEngineTime,true);
	seqDb.append(db);
	db = ASNLib::encodeOctetString(m_msgUserName,true);
	seqDb.append(db);
	db = ASNLib::encodeOctetString(m_msgAuthenticationParameters,true);
	seqDb.append(db);
	db = ASNLib::encodeOctetString(m_msgPrivacyParameters,true);
	seqDb.append(db);
	length = ASNLib::encodeSequence(seqDb,true);
	data.append(seqDb);
	return length;
}

void UsmSecurityParameters::getParams(NamedList* params)
{}

void UsmSecurityParameters::setParams(NamedList* params)
{}

/**
  * SysOREntry
  */
SysOREntry::SysOREntry()
{
	m_sysORDescr = new DisplayString();
	m_sysORUpTime = new TimeStamp();
}

SysOREntry::SysOREntry(void* data, int len)
{
	m_sysORDescr = new DisplayString();
	m_sysORUpTime = new TimeStamp();

	DataBlock db(data,len);
	decode(db);
}

SysOREntry::~SysOREntry()
{
	TelEngine::destruct(m_sysORDescr);
	TelEngine::destruct(m_sysORUpTime);
}

int SysOREntry::decode(DataBlock& data)
{
	int length = 0;
	length = ASNLib::decodeSequence(data,true);
	if (length < 0)
		return length;
	length = ASNLib::decodeINT32(data,&m_sysORIndex,true);
	length = ASNLib::decodeOID(data,&m_sysORID,true);
	length = m_sysORDescr->decode(data);
	length = m_sysORUpTime->decode(data);
	return length;
}

int SysOREntry::encode(DataBlock& data)
{
	int length = -1;
	DataBlock seqDb;
	DataBlock db;
	length = 0;
	db =  ASNLib::encodeInteger(m_sysORIndex,true);
	seqDb.append(db);
	db = ASNLib::encodeOID(m_sysORID,true);
	seqDb.append(db);
	m_sysORDescr->encode(seqDb);
	m_sysORUpTime->encode(seqDb);
	length = ASNLib::encodeSequence(seqDb,true);
	data.append(seqDb);
	return length;
}

void SysOREntry::getParams(NamedList* params)
{}

void SysOREntry::setParams(NamedList* params)
{}

}
