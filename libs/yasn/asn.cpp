/**
 * asn.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * ASN.1 Library
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2014 Null Team
 *
 * This software is distributed under multiple licenses;
 * see the COPYING file in the main directory for licensing
 * information for this specific distribution.
 *
 * This use of this software may be subject to additional restrictions.
 * See the LEGAL file in the main directory for details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include "yateasn.h"

using namespace TelEngine;

static String s_libName = "ASNLib";

ASNLib::ASNLib()
{}

ASNLib::~ASNLib()
{}

int ASNLib::decodeLength(DataBlock& data) {

    XDebug(s_libName.c_str(),DebugAll,"::decodeLength() - from data='%p'",&data);
    int length = 0;
    uint8_t lengthByte = data[0];

    if (lengthByte & ASN_LONG_LENGTH) { // the length is represented on more than one byte

	lengthByte &= ~ASN_LONG_LENGTH;	/* turn MSB off */
	if (lengthByte == 0) {
	    data.cut(-1);
	    return IndefiniteForm;
	}

	if (lengthByte > sizeof(int))
	    return InvalidLengthOrTag;

	for (int i = 0 ; i < lengthByte ; i++)
	    length = (length << 8) + data[1 + i];

	data.cut(-lengthByte - 1);
	return length;

    } else { // one byte for length
	length = (int) lengthByte;
	data.cut(-1);
	return length;
    }
}

DataBlock ASNLib::buildLength(DataBlock& data)
{
    XDebug(s_libName.c_str(),DebugAll,"::buildLength() - encode length=%d",data.length());
    DataBlock lenDb;
    if (data.length() < 0)
	return lenDb;
    if (data.length() < ASN_LONG_LENGTH) {
	uint8_t l = data.length();
	lenDb.append(&l, 1);
	return lenDb;
    }
    else {
    	uint8_t longLen = ASN_LONG_LENGTH;
	int len = data.length();
	while (len > 0) {
	    uint8_t v = len & 0xFF;
	    lenDb.insert(DataBlock(&v,1));
	    len >>= 8;
	}
	longLen |= lenDb.length();
	lenDb.insert(DataBlock(&longLen,1));
	return lenDb;
    }
   return lenDb;
}

int ASNLib::matchEOC(DataBlock& data)
{
    /**
     * EoC = 00 00
     */
    XDebug(s_libName.c_str(),DebugAll,"::matchEOC() in data='%p'",&data);
    if (data.length() < 2)
	return InvalidLengthOrTag;
    if (data[0] == 0 && data[1] == 0) {
    	data.cut(-2);
    	return 2;
    }
    return InvalidLengthOrTag;
}


int ASNLib::parseUntilEoC(DataBlock& data, int length)
{
    if (length >= (int)data.length() || ASNLib::matchEOC(data) > 0)
	return length;
    while (data.length() && ASNLib::matchEOC(data) < 0) {
	// compute tag portion length
	AsnTag tag;
	AsnTag::decode(tag,data);
	length += tag.coding().length();
	data.cut(-(int)tag.coding().length());
	// compute length portion length
	int initLen = data.length();
	int len = ASNLib::decodeLength(data);
	length += initLen - data.length();

	bool checkEoC = (len == ASNLib::IndefiniteForm);
	if (!checkEoC && len < 0)
	    return length;

	if (checkEoC) {
	    length = parseUntilEoC(data,length);
	    if (ASNLib::matchEOC(data) > 0)
		length += 2;
	}
	else {
	    length += len;
	    data.cut(-len);
	}
    }
    return length;
}

int ASNLib::decodeBoolean(DataBlock& data, bool* val, bool tagCheck)
{
    /**
     * boolean = 0x01 length byte (byte == 0 => false, byte != 0 => true)
     */
    XDebug(s_libName.c_str(),DebugAll,"::decodeBoolean() from data='%p'",&data);
    if (data.length() < 2)
	return InvalidLengthOrTag;
#ifdef DEBUG
    unsigned int initLen = data.length();
#endif
    if (tagCheck) {
	int type = data[0];
	if ((type != BOOLEAN)) {
	    XDebug(s_libName.c_str(),DebugAll,"::decodeBoolean() - Invalid Tag in data='%p'",&data);
	    return InvalidLengthOrTag;
	}
	data.cut(-1);
    }
    int length = decodeLength(data);
    if (length < 0) {
	DDebug(s_libName.c_str(),DebugAll,"::decodeBoolean() - Invalid Length in data='%p'",&data);
	return length;
    }

    if ((unsigned int)length > data.length() || length != 1) {
	DDebug(s_libName.c_str(),DebugAll,"::decodeBoolean() - Invalid Length in data='%p'",&data);
	return InvalidLengthOrTag;
    }
    if (!val) {
        data.cut(-1);
        DDebug(s_libName.c_str(),DebugAll,"::decodeBoolean() - Invalid buffer for return data");
        return InvalidContentsError;
    }
    *val = false;
    if ((data[0] & 0xFF) != 0)
	*val = true;
    data.cut(-1);
#ifdef DEBUG
    Debug(s_libName.c_str(),DebugAll,"::decodeBoolean() - decoded boolean value from data='%p', consumed %u bytes",
    	&data, initLen - data.length());
#endif
    return length;
}

int ASNLib::decodeInteger(DataBlock& data, u_int64_t& intVal, unsigned int bytes, bool tagCheck)
{
    /**
     * integer = 0x02 length byte {byte}*
     */
    XDebug(s_libName.c_str(),DebugAll,"::decodeInteger() from  data='%p'",&data);
    int64_t value = 0;
    if (data.length() < 2)
	return InvalidLengthOrTag;
#ifdef DEBUG
    unsigned int initLen = data.length();
#endif
    if (tagCheck) {
	int type = data[0];
	if ((type != INTEGER)) {
	    XDebug(s_libName.c_str(),DebugAll,"::decodeInteger() - Invalid Tag in data='%p'",&data);
	    return InvalidLengthOrTag;
	}
	data.cut(-1);
    }
    int length = decodeLength(data);
    if (length < 0) {
	DDebug(s_libName.c_str(),DebugAll,"::decodeInteger() - Invalid Length in data='%p'",&data);
	return length;
    }

    if ((unsigned int)length > data.length()) {
	DDebug(s_libName.c_str(),DebugAll,"::decodeInteger() - Invalid Length in data='%p'",&data);
	return InvalidLengthOrTag;
    }

    if ((unsigned int)length > bytes) {
    	DDebug(s_libName.c_str(),DebugAll,"::decodeInteger() - Invalid Length: decoded length=%d greater than requested length=%u in data='%p'",
    			length,bytes,&data);
    	return InvalidLengthOrTag;
    }

    if (data[0] & 0x80)
	value = -1; /* integer is negative */
    int j = 0;
    while (j < length) {
	value = (value << 8) | data[j];
	j++;
    }
    intVal = (u_int64_t) value;
    data.cut(-length);
#ifdef DEBUG
    Debug(s_libName.c_str(),DebugAll,"::decodeInteger() - decoded integer value from  data='%p', consumed %u bytes",
    	&data, initLen - data.length());
#endif
    return length;
}

int ASNLib::decodeUINT8(DataBlock& data, u_int8_t* intVal, bool tagCheck)
{
    XDebug(s_libName.c_str(),DebugAll,"::decodeUINT8()");
    u_int64_t val;
    int l = decodeInteger(data,val,sizeof(u_int8_t),tagCheck);
    if (!intVal) {
        DDebug(s_libName.c_str(),DebugAll,"::decodeUINT8() - Invalid buffer for return data");
        return InvalidContentsError;
    }
    *intVal = (u_int8_t) val;
    return l;
}

int ASNLib::decodeUINT16(DataBlock& data, u_int16_t* intVal, bool tagCheck)
{
    XDebug(s_libName.c_str(),DebugAll,"::decodeUINT16() from data='%p'",&data);
    u_int64_t val;
    int l = decodeInteger(data,val,sizeof(u_int16_t),tagCheck);
    if (!intVal) {
        DDebug(s_libName.c_str(),DebugAll,"::decodeUINT16() - Invalid buffer for return data");
        return InvalidContentsError;
    }
    *intVal = (u_int16_t) val;
    return l;
}

int ASNLib::decodeUINT32(DataBlock& data, u_int32_t* intVal, bool tagCheck)
{
    XDebug(s_libName.c_str(),DebugAll,"::decodeUINT32() from data='%p'",&data);
    u_int64_t val;
    int l = decodeInteger(data,val,sizeof(u_int32_t),tagCheck);
    if (!intVal) {
        DDebug(s_libName.c_str(),DebugAll,"::decodeUINT32() - Invalid buffer for return data");
        return InvalidContentsError;
    }
    *intVal = (u_int32_t) val;
    return l;
}

int ASNLib::decodeUINT64(DataBlock& data, u_int64_t* intVal, bool tagCheck)
{
    XDebug(s_libName.c_str(),DebugAll,"::decodeUINT64() from data='%p'",&data);
    u_int64_t val;
    int l = decodeInteger(data,val,sizeof(u_int64_t),tagCheck);
    if (!intVal) {
        DDebug(s_libName.c_str(),DebugAll,"::decodeUINT64() - Invalid buffer for return data");
        return InvalidContentsError;
    }
    *intVal = val;
    return l;
}

int ASNLib::decodeINT8(DataBlock& data, int8_t* intVal, bool tagCheck)
{
    XDebug(s_libName.c_str(),DebugAll,"::decodeINT8() from data='%p'",&data);
    u_int64_t val;
    int l = decodeInteger(data,val,sizeof(int8_t),tagCheck);
    if (!intVal) {
        DDebug(s_libName.c_str(),DebugAll,"::decodeINT8() - Invalid buffer for return data");
        return InvalidContentsError;
    }
    *intVal = (int8_t) val;
    return l;
}

int ASNLib::decodeINT16(DataBlock& data, int16_t* intVal, bool tagCheck)
{
    XDebug(s_libName.c_str(),DebugAll,"::decodeINT16() from data='%p'",&data);
    u_int64_t val;
    int l = decodeInteger(data,val,sizeof(int16_t),tagCheck);
    if (!intVal) {
        DDebug(s_libName.c_str(),DebugAll,"::decodeINT16() - Invalid buffer for return data");
        return InvalidContentsError;
    }
    *intVal = (int16_t) val;
    return l;
}

int ASNLib::decodeINT32(DataBlock& data, int32_t* intVal, bool tagCheck)
{
    XDebug(s_libName.c_str(),DebugAll,"::decodeINT32() from data='%p'",&data);
    u_int64_t val;
    int l = decodeInteger(data,val,sizeof(int32_t),tagCheck);
    if (!intVal) {
        DDebug(s_libName.c_str(),DebugAll,"::decodeINT32() - Invalid buffer for return data");
        return InvalidContentsError;
    }
    *intVal = (int32_t) val;
    return l;
}

int ASNLib::decodeINT64(DataBlock& data, int64_t* intVal, bool tagCheck)
{
    XDebug(s_libName.c_str(),DebugAll,"::decodeINT64() from data='%p'",&data);
    u_int64_t val;
    int l = decodeInteger(data,val,sizeof(int64_t),tagCheck);
    if (!intVal) {
        DDebug(s_libName.c_str(),DebugAll,"::decodeINT64() - Invalid buffer for return data");
        return InvalidContentsError;
    }
    *intVal = val;
    return l;
}

int ASNLib::decodeBitString(DataBlock& data, String* val, bool tagCheck)
{
    /**
     * bitstring ::= 0x03 asnlength unusedBytes {byte}*
     */
    XDebug(s_libName.c_str(),DebugAll, "::decodeBitString() from data='%p'",&data);
    if (data.length() < 2)
	return InvalidLengthOrTag;
#ifdef DEBUG
    unsigned int initLen = data.length();
#endif
    if (tagCheck) {
	int type = data[0];
	if ((type != BIT_STRING)) {
	    XDebug(s_libName.c_str(),DebugAll,"::decodeBitString() - Invalid Tag in data='%p'",&data);
	    return InvalidLengthOrTag;
	}
	data.cut(-1);
    }
    int length = decodeLength(data);
    if (length < 0) {
	DDebug(s_libName.c_str(),DebugAll,"::decodeBitString() - Invalid Length in data='%p'",&data);
	return length;
    }

    if ((unsigned int)length > data.length()) {
	DDebug(s_libName.c_str(),DebugAll,"::decodeBitString() - Invalid Length in data='%p'",&data);
	return InvalidLengthOrTag;
    }

    if (data[0] > 7){
	DDebug(s_libName.c_str(),DebugAll, "::decodeBitString() - Invalid bitstring, unused bytes > 7 in data='%p'",&data);
	return InvalidLengthOrTag;
    }
    int unused = data[0];
    data.cut(-1);
    length--;
    int j = 0;
    if (!val) {
        DDebug(s_libName.c_str(),DebugAll,"::decodeBitString() - Invalid buffer for return data");
        data.cut(-length);
        return InvalidContentsError;
    }
    *val = "";
    while (j < length) {
	uint8_t byte = data[j];
	for (int i = 7; i > -1; i--) {
	    int c = (byte >> i) % 2;
	    *val += c;
	}
	j++;
    }
    *val = val->substr(0, length * 8 - unused);
    data.cut(-length);
#ifdef DEBUG
    Debug(s_libName.c_str(),DebugAll,"::decodeBitString() - decoded bit string value from  data='%p', consumed %u bytes",
    	&data, initLen - data.length());
#endif
    return length;
}

int ASNLib::decodeOctetString(DataBlock& db, OctetString* strVal, bool tagCheck)
{
    /**
     *  octet string ::= 0x04 asnlength {byte}*
     */
    XDebug(s_libName.c_str(),DebugAll,":decodeOctetString() from data='%p'",&db);
    if (db.length() < 2)
	return InvalidLengthOrTag;
#ifdef DEBUG
    unsigned int initLen = db.length();
#endif
    if (tagCheck) {
	int type = db[0];
	if (type != OCTET_STRING) {
	    XDebug(s_libName.c_str(),DebugAll,"::decodeOctetString() - Invalid Tag in data='%p'",&db);
	    return InvalidLengthOrTag;
	}
	db.cut(-1);
    }
    int length = decodeLength(db);
    if (length < 0) {
	DDebug(s_libName.c_str(),DebugAll,"::decodeOctetString() - Invalid Length in data='%p'",&db);
	return length;
    }
    if ((unsigned int)length > db.length()) {
	DDebug(s_libName.c_str(),DebugAll,"::decodeOctetString() - Invalid Length in data='%p'",&db);
	return InvalidLengthOrTag;
    }
    if (!strVal) {
        DDebug(s_libName.c_str(),DebugAll,"::decodeOctetString() - Invalid buffer for return data");
        return InvalidContentsError;
    }
    strVal->assign((void*)db.data(0,length),length);
    db.cut(-length);
#ifdef DEBUG
    Debug(s_libName.c_str(),DebugAll,"::decodeOctetString() - decoded octet string value from  data='%p', consumed %u bytes",
    	&db, initLen - db.length());
#endif
    return length;
}

int ASNLib::decodeNull(DataBlock& data, bool tagCheck)
{
    /**
     * ASN.1 null := 0x05 00
     */
    XDebug(s_libName.c_str(),DebugAll,"::decodeNull() from data='%p'",&data);
    if (tagCheck) {
	if (data.length() < 2)
	    return InvalidLengthOrTag;

	if (data[0] != NULL_ID) {
	    XDebug(s_libName.c_str(),DebugAll, "::decodeNull() - Invalid Tag in data='%p'",&data);
	    return InvalidLengthOrTag;
	}
	data.cut(-1);
    }
    int length = decodeLength(data);
    if (length != 0) {
	DDebug(s_libName.c_str(),DebugAll,"::decodeNull() - Invalid Length in data='%p'",&data);
	return InvalidLengthOrTag;;
    }
    DDebug(s_libName.c_str(),DebugAll,"::decodeNull() - decoded null value from  data='%p', consumed %u bytes",
    		&data, (tagCheck ? 2 : 1));
    return length;
}

int ASNLib::decodeOID(DataBlock& data, ASNObjId* obj, bool tagCheck)
{
   /**
    * ASN.1 objid ::= 0x06 asnlength subidentifier {subidentifier}*
    * subidentifier ::= {leadingbyte}* lastbyte
    * leadingbyte ::= 1 7bites
    * lastbyte ::= 0 7bites
    */
    XDebug(s_libName.c_str(),DebugAll,"::decodeOID() from data='%p'",&data);
    if (data.length() < 2)
	return InvalidLengthOrTag;
#ifdef DEBUG
    unsigned int initLen = data.length();
#endif
    if (tagCheck) {
	if (data[0] != OBJECT_ID) {
	    XDebug(s_libName.c_str(),DebugAll,"::decodeOID() - Invalid Tag in data='%p'",&data);
	    return InvalidLengthOrTag;
	}
	data.cut(-1);
    }
    int length = decodeLength(data);
    if (length < 0) {
	DDebug(s_libName.c_str(),DebugAll,"::decodeOID() - Invalid Length in data='%p'",&data);
	return length;
    }

    if ((unsigned int)length > data.length()) {
	DDebug(s_libName.c_str(),DebugAll,"::decodeOID() - Invalid Length in data='%p'",&data);
	return InvalidLengthOrTag;
    }

    if (length == 0) {
	obj = 0;
	return length;
    }

    int j = 0;
    String oid = "";
    unsigned int longNo = 0;
    while (j < length) {
    // first byte contains 2 identifiers : x,y. The byte is 40 * x + y . x can only be 0,1,2 so if x > 2, x stays 2 and the rest goes into y
	if (j == 0) {
	    unsigned int x = data[j] / 40;
	    unsigned int y = data[j] % 40;
	    if (x > 2) {
		y = (x - 2) * 40 + y;
		x = 2;
	    }
	    oid += x;
	    oid += ".";
	    oid += y;
	    oid += ".";
	}
	else {
	    uint8_t byte = data[j];
	    longNo += byte & ~ASN_BIT8;
	    if ((byte & ASN_BIT8) == ASN_BIT8)
		longNo <<= 7;
	    else {
		oid += longNo;
		longNo = 0;
		if (j != length -1)
		    oid += ".";
	    }
      }
      j++;
    }
    data.cut(-length);
    if (!obj) {
        DDebug(s_libName.c_str(),DebugAll,"::decodeOID() - Invalid buffer for return data");
        return InvalidContentsError;
    }
    *obj = oid;
#ifdef DEBUG
    Debug(s_libName.c_str(),DebugAll,"::decodeOID() - decoded object ID from  data='%p', consumed %u bytes",
    	&data, initLen - data.length());
#endif
    return length;
}

int ASNLib::decodeReal(DataBlock& db, float* realVal, bool tagCheck)
{
    if (db.length() < 2)
	return InvalidLengthOrTag;
    unsigned int initLen = db.length();
    if (tagCheck) {
	if (db[0] != REAL) {
	    XDebug(s_libName.c_str(),DebugAll,"::decodeReal() - Invalid Tag in data='%p'",&db);
	    return InvalidLengthOrTag;
	}
	db.cut(-1);
    }
    int length = decodeLength(db);
    if (length < 0) {
	DDebug(s_libName.c_str(),DebugAll,"::decodeReal() - Invalid Length in data='%p'",&db);
	return length;
    }
    if ((unsigned int)length > db.length()) {
	DDebug(s_libName.c_str(),DebugAll,"::decodeReal() - Invalid Length in data='%p'",&db);
	return InvalidLengthOrTag;
    }
    db.cut(-length);
    Debug(s_libName.c_str(),DebugInfo,"::decodeReal() - real value decoding not implemented, skipping over the %u bytes of the encoding",
    		initLen - db.length());
    return 0;
}

int ASNLib::decodeString(DataBlock& data, String* str, int* type, bool tagCheck)
{
    XDebug(s_libName.c_str(),DebugAll,"::decodeString() from data='%p'",&data);
    if (data.length() < 2)
	return InvalidLengthOrTag;
#ifdef DEBUG
    unsigned int initLen = data.length();
#endif
    if (tagCheck) {
	if (data[0] != NUMERIC_STR ||
	    data[0] != PRINTABLE_STR ||
	    data[0] != IA5_STR ||
	    data[0] != VISIBLE_STR
	    ) {
	    XDebug(s_libName.c_str(),DebugAll,"::decodeString() -  Invalid Tag in data='%p'",&data);
	    return InvalidLengthOrTag;
	}
	if (type)
	    *type = data[0];
	data.cut(-1);
    }
    int length = decodeLength(data);
    if (length < 0) {
	DDebug(s_libName.c_str(),DebugAll,"::decodeString() -Invalid Length in data='%p'",&data);
	return length;
    }

    if ((unsigned int)length > data.length()) {
	DDebug(s_libName.c_str(),DebugAll,"::decodeString() -Invalid Length in data='%p'",&data);
	return InvalidLengthOrTag;
    }

    String var = "";
    for (int i = 0; i < length; i++)
	var += (char) (data[i] & 0x7f);
    data.cut(-length);
    if (!str || !type) {
        DDebug(s_libName.c_str(),DebugAll,"::decodeString() - Invalid buffer for return data");
        return InvalidContentsError;
    }
    *str = var;
#ifdef DEBUG
    Debug(s_libName.c_str(),DebugInfo,"::decodeString() - decode string value from data='%p', consumed %u bytes",
    	&data,initLen - data.length());
#endif
    return length;
}


int ASNLib::decodeUtf8(DataBlock& data, String* str, bool tagCheck)
{
    XDebug(s_libName.c_str(),DebugAll,"::decodeUtf8() from data='%p'",&data);
    if (data.length() < 2)
	return InvalidLengthOrTag;
#ifdef DEBUG
    unsigned int initLen = data.length();
#endif
    if (tagCheck) {
	if (data[0] != UTF8_STR) {
	    XDebug(s_libName.c_str(),DebugAll,"::decodeUtf8() - Invalid Tag in data='%p'",&data);
	    return InvalidLengthOrTag;
	}
	data.cut(-1);
    }
    int length = decodeLength(data);
    if (length < 0) {
	DDebug(s_libName.c_str(),DebugAll,"::decodeUtf8() -Invalid Length in data='%p'",&data);
	return length;
    }

    if ((unsigned int)length > data.length()) {
	Debug(s_libName.c_str(),DebugAll,"::decodeUtf8() - Invalid Length in data='%p'",&data);
	return InvalidLengthOrTag;
    }

    String var = "";
    for (int i = 0; i < length; i++)
	var += (char) (data[i]);
    data.cut(-length);
    if (String::lenUtf8(var.c_str()) < 0)
	return ParseError;
    if (!str) {
        DDebug(s_libName.c_str(),DebugAll,"::decodeUTF8() - Invalid buffer for return data");
        return InvalidContentsError;
    }
    *str = var;
#ifdef DEBUG
    Debug(s_libName.c_str(),DebugAll,"::decodeUtf8() - decoded an UTF8 string value from data='%p', consumed %u bytes",&data,initLen - data.length());
#endif
    return length;
}

int ASNLib::decodeGenTime(DataBlock& data, unsigned int* time, unsigned int* fractions, bool* utc, bool tagCheck)
{
    XDebug(s_libName.c_str(),DebugAll,"::decodeGenTime() from data='%p'",&data);
    if (data.length() < 2)
	return InvalidLengthOrTag;
#ifdef DEBUG
    unsigned int initLen = data.length();
#endif
    if (tagCheck) {
	if (data[0] != GENERALIZED_TIME) {
	    XDebug(s_libName.c_str(),DebugAll,"::decodeGenTime() - Invalid Tag in data='%p'",&data);
	    return InvalidLengthOrTag;
	}
	data.cut(-1);
    }
    int length = decodeLength(data);
    if (length < 0) {
	DDebug(s_libName.c_str(),DebugAll,"::decodeGenTime() - Invalid Length in data='%p'",&data);
	return length;
    }

    if ((unsigned int)length > data.length() || length < 14) {
	DDebug(s_libName.c_str(),DebugAll,"::decodeGenTime() - Invalid Length in data='%p'",&data);
	return InvalidLengthOrTag;
    }
    String date = "";
    for (int i = 0; i < length; i++)
	date += (char) (data[i]);
    data.cut(-length);

    if (!(utc && fractions && time)) {
        DDebug(s_libName.c_str(),DebugAll,"::decodeGenTime() - Invalid buffer for return data");
        return InvalidContentsError;
    }

    unsigned int year, month, day, hours, minutes, seconds;
    int timeDiff = 0;

    *utc = false;
    *fractions = 0;

    if (date[date.length() - 1] == 'Z') {
	*utc = true;
	date = date.substr(0,date.length() - 1);
    }
    else {
	int pos = date.find('-');
	if (pos < 0)
	    pos = date.find('+');
	if (pos >0 && pos != (int)date.length() - 5)
	    return InvalidContentsError;
	if (pos > 0) {
	    char sign = date.at(pos);
	    unsigned int hDiff = (unsigned int) date.substr(date.length() - 4,2).toInteger(-1,10);
	    if (hDiff > 11)
		return InvalidContentsError;
	    unsigned int mDiff = (unsigned int) date.substr(date.length() - 2,2).toInteger(-1,10);
	    if (mDiff > 59)
		return InvalidContentsError;
	    unsigned int diff = Time::toEpoch(1970,1,1,hDiff,mDiff,0);
	    if (sign == '-')
		timeDiff = diff;
	    else
		timeDiff -= diff;
	    *utc = true;
	    date = date.substr(0,date.length() - 5);
	}
    }
    ObjList* list = date.split('.');
    if (!list || list->count() > 2)
	return InvalidContentsError;
    if (list->count() == 2)
	*fractions = (*list)[1]->toString().toInteger(0,10);

    String dateTime = (*list)[0]->toString();
    TelEngine::destruct(list);

    year = dateTime.substr(0,4).toInteger(-1,10);
    month = dateTime.substr(4,2).toInteger(-1,10);
    day = dateTime.substr(6,2).toInteger(-1,10);
    hours = dateTime.substr(8,2).toInteger(-1,10);
    minutes = dateTime.substr(10,2).toInteger(-1,10);
    seconds = dateTime.substr(12,2).toInteger(-1,10);
    if (year < 1970 || month > 12 || day > 31 || hours > 23 || minutes > 59 || seconds > 59)
	return InvalidContentsError;

    unsigned int epochTime = Time::toEpoch(year,month,day,hours,minutes,seconds);
    if (epochTime == (unsigned int) -1)
	return InvalidContentsError;
    *time = epochTime + timeDiff;
#ifdef DEBUG
    Debug(s_libName.c_str(),DebugAll,"::decodeGenTime() - decoded time value from data='%p', consumed %u bytes",&data,initLen - data.length());
#endif
    return length;
}

int ASNLib::decodeUTCTime(DataBlock& data, unsigned int* time, bool tagCheck)
{
    XDebug(s_libName.c_str(),DebugAll,"::decodeUTCTime() from data='%p'",&data);
    if (data.length() < 2)
	return InvalidLengthOrTag;
#ifdef DEBUG
    unsigned int initLen = data.length();
#endif
    if (tagCheck) {
	if (data[0] != UTC_TIME) {
	    XDebug(s_libName.c_str(),DebugAll,"::decodeUTCTime() - Invalid Tag in data='%p'",&data);
	    return InvalidLengthOrTag;
	}
	data.cut(-1);
    }
    int length = decodeLength(data);
    if (length < 0) {
	DDebug(s_libName.c_str(),DebugAll,"::decodeUTCTime() - Invalid Length in data='%p'",&data);
	return length;
    }

    if ((unsigned int)length > data.length() || length < 11) {
	DDebug(s_libName.c_str(),DebugAll,"::decodeUTCTime() - Invalid Length in data='%p'",&data);
	return InvalidLengthOrTag;
    }
    String date = "";
    for (int i = 0; i < length; i++)
	date += (char) (data[i]);
    data.cut(-length);

    if (!time) {
        DDebug(s_libName.c_str(),DebugAll,"::decodeUTCTime() - Invalid buffer for return data");
        return InvalidContentsError;
    }
    unsigned int year = 0, month = 0, day = 0, hours = 0, minutes = 0, seconds = 0;
    int timeDiff = 0;

    int len = date.length();
    if (date[date.length() - 1] == 'Z')
	date = date.substr(0,len - 1);
    else {
	int pos = date.find('-');
	if (pos < 0)
	    pos = date.find('+');
	if ((pos >0 && pos != len - 5) || pos < 0)
	    return InvalidContentsError;
	if (pos > 0) {
	    char sign = date.at(pos);
	    unsigned int hDiff = (unsigned int) date.substr(len - 4,2).toInteger(-1,10);
	    if (hDiff > 11)
		return InvalidContentsError;
	    unsigned int mDiff = (unsigned int) date.substr(len - 2,2).toInteger(-1,10);
	    if (mDiff > 59)
		return InvalidContentsError;
	    unsigned int diff = Time::toEpoch(1970,1,1,hDiff,mDiff,0);
	    if (sign == '-')
		timeDiff = diff;
	    else
		timeDiff -= diff;
	    date = date.substr(0,len - 5);
	}
    }
    year = date.substr(0,2).toInteger(-1,10);
    year = (year > 50) ? 1900 + year : 2000 + year;
    month = date.substr(2,2).toInteger(-1,10);
    day = date.substr(4,2).toInteger(-1,10);
    hours = date.substr(6,2).toInteger(-1,10);
    minutes = date.substr(8,2).toInteger(-1,10);
    if (date.length() > 10)
	seconds = date.substr(10,2).toInteger(-1,10);
    if (year < 1970 || month > 12 || day > 31 || hours > 23 || minutes > 59 || seconds > 59)
	return InvalidContentsError;

    unsigned int epochTime = Time::toEpoch(year,month,day,hours,minutes,seconds);
    if (epochTime == (unsigned int) -1)
	return InvalidContentsError;
    *time = epochTime + timeDiff;
#ifdef DEBUG
    Debug(s_libName.c_str(),DebugAll,"::decodeUTCTime() - decoded time value from data='%p', consumed %u bytes",&data,initLen - data.length());
#endif
    return length;
}

int ASNLib::decodeAny(DataBlock data, DataBlock* val, bool tagCheck)
{
    XDebug(s_libName.c_str(),DebugAll,"::decodeAny() from data='%p'",&data);
    if (!val) {
        DDebug(s_libName.c_str(),DebugAll,"::decodeAny() - Invalid buffer for return data");
        return InvalidContentsError;
    }
    val->append(data);
    return data.length();
}

int ASNLib::decodeSequence(DataBlock& data, bool tagCheck)
{
    XDebug(s_libName.c_str(),DebugAll,"::decodeSequence() from data='%p'",&data);
    if (data.length() < 2)
	return InvalidLengthOrTag;
#ifdef DEBUG
    unsigned int initLen = data.length();
#endif
    if (tagCheck) {
	if (data[0] != SEQUENCE) {
	    DDebug(s_libName.c_str(),DebugAll,"::decodeSequence() - Invalid Tag in data='%p'",&data);
	    return InvalidLengthOrTag;
	}
	data.cut(-1);
    }
    int length = decodeLength(data);
    if (length < 0)
	Debug(s_libName.c_str(),DebugAll,"::decodeSequence() - Invalid Length in data='%p'",&data);
#ifdef DEBUG
    Debug(s_libName.c_str(),DebugAll,"::decodeSequence() - decoded sequence tags from data='%p', consumed %u bytes",&data,initLen - data.length());
#endif
    return length;
}

int ASNLib::decodeSet(DataBlock& data, bool tagCheck)
{
    XDebug(s_libName.c_str(),DebugAll,"::decodeSet() from data='%p",&data);
    if (data.length() < 2)
	return InvalidLengthOrTag;
#ifdef DEBUG
    unsigned int initLen = data.length();
#endif
    if (tagCheck) {
	if (data[0] != SET) {
	    DDebug(s_libName.c_str(),DebugAll,"::decodeSet() - Invalid Tag in data='%p'",&data);
	    return InvalidLengthOrTag;
	}
	data.cut(-1);
    }
    int length = decodeLength(data);
#ifdef DEBUG
    if (length < 0)
	Debug(s_libName.c_str(),DebugAll,"::decodeSet() - Invalid Length in data='%p'",&data);
    else
	Debug(s_libName.c_str(),DebugAll,"::decodeSet() - decoded set tags from data='%p', consumed %u bytes",&data,initLen - data.length());
#endif
    return length;
}

DataBlock ASNLib::encodeBoolean(bool val, bool tagCheck)
{
    /**
     * ASN.1 boolean ::= 0x01 asnlength=0x01 byte
     */
    DataBlock data;
    uint8_t b = BOOLEAN;
    if (tagCheck) {
	data.append(&b, 1);
	b = 1;
	data.append(&b, 1);
    }
    b = val ? 1 : 0;
    data.append(&b, 1);
    XDebug(s_libName.c_str(),DebugAll,"::encodeBoolean('%s') - encoded boolean value into %u bytes",String::boolText(val),data.length());
    return data;
}

DataBlock ASNLib::encodeInteger(u_int64_t intVal, bool tagCheck)
{
    /**
     * ASN.1 integer ::= 0x02 asnlength byte {byte}*
     */
    DataBlock data;
    uint8_t tag = INTEGER;

    // 9 consecutive ones or zeros are not allowed at the beginning of an integer
    int size = sizeof(u_int64_t);
    uint16_t msb = (uint16_t)(intVal >> ((size - 1) * 8 - 1));

    while (((msb & 0x1FF) == 0 || (msb & 0x1FF) == 0x1FF) && (size - 1 >= 1)) {
	size--;
	msb = (uint16_t)(intVal >> ((size - 1) * 8 - 1));
    }
    if (size == 0)
	return data;

    DataBlock contents;
    while(size) {
	uint8_t byte = (uint8_t)(intVal >> ((size - 1) * 8));
	contents.append(&byte, 1);
	size--;
    }

    if (contents.length() == 0)
	return data;
    if (tagCheck) {
	data.append(&tag, 1);
	DataBlock len = buildLength(contents);
	data.append(len);
    }
    data.append(contents);
    XDebug(s_libName.c_str(),DebugAll,"::encodeInteger('" FMT64 "') - encoded into %u bytes",intVal,data.length());
    return data;
}

DataBlock ASNLib::encodeBitString(String val, bool tagCheck)
{
    /**
     * ASN.1 bit string ::= 0x03 asnlength unused {byte}*
     */
    DataBlock data;
    uint8_t tag = BIT_STRING;
    DataBlock contents;

    int l = val.length();
    uint8_t trail = (-l) & 0x07;
    for (int i = 0; i < trail; i++)
	val += "0";
    contents.append(&trail, 1);

    for (unsigned int i = 0; i < val.length(); i += 8) {
	uint8_t hex = val.substr(i, 8).toInteger(0,2);
	contents.append(&hex, 1);
    }

    if (tagCheck) {
	data.append(&tag,1);
	DataBlock len = buildLength(contents);
	data.append(len);
    }
    data.append(contents);
    XDebug(s_libName.c_str(),DebugAll,"::encodeBitString('%s') - encoded bit string into %u bytes",val.c_str(),data.length());
    return data;
}

DataBlock ASNLib::encodeOctetString(OctetString strVal, bool tagCheck)
{
    /**
      * ASN.1 octet string ::= 0x04 asnlength byte {byte}*
      */
    DataBlock data;
    uint8_t tag = OCTET_STRING;
    if (tagCheck) {
	data.append(&tag, 1);
	DataBlock len = buildLength(strVal);
	data.append(len);
    }
    data.append(strVal);
    XDebug(s_libName.c_str(),DebugAll,"ASNLib::encodeOctetString('%s') - encoded octet string into %u bytes",
    		strVal.toHexString().c_str(),data.length());
    return data;
}

DataBlock ASNLib::encodeNull(bool tagCheck)
{
    /**
      *	ASN.1 null ::= 0x05 00
      */
    XDebug(s_libName.c_str(),DebugAll,"::encodeNull()");
    DataBlock data;
    uint8_t tag = NULL_ID;
    if (tagCheck) {
	data.append(&tag, 1);
	tag = 0;
	data.append(&tag, 1);
    }
    return data;
}

DataBlock ASNLib::encodeOID(ASNObjId obj, bool tagCheck)
{
    /**
      * ASN.1 object id ::= 0x06 asnlength byte {byte}*
      */
    DataBlock data;
    uint8_t tag = OBJECT_ID;

    DataBlock cont = obj.getIds();
    DataBlock contents;

    if (cont.length() == 0)
	return data;

    // first byte is built following the rule first = 40 * x + y
    // x must not be greater than 2 (joint-iso-ccitt identifier)
    if (cont[0] > 2) {
    	Debug(s_libName.c_str(),DebugAll,"::encodeOID('%s') - first identifier is greater than the maximum allowed identifier 'joint-iso-ccitt'(2)",
    			obj.toString().c_str());
    	return data;
    }
    uint8_t first = 40 * cont[0];
    if (cont.length() > 1) {
    	// y must not be greater than 39 if x < 2
    	if (cont[0] < 2 && cont[1] > 39) {
    	    Debug(s_libName.c_str(),DebugAll,"::encodeOID('%s') - cannot encode second identifier, its value is not allowed for the first identifier",
    			obj.toString().c_str());
    	    return data;
    	}
    	first += cont[1];
    	cont.cut(-1);
    }
    contents.append(&first, 1);
    cont.cut(-1);

    contents.append(cont);
    if (tagCheck) {
	data.append(&tag,1);
	DataBlock len = buildLength(contents);
	data.append(len);
    }
    data.append(contents);
    XDebug(s_libName.c_str(),DebugAll,"::encodeOID('%s') - encoded object ID into %u bytes",obj.toString().c_str(),data.length());
    return data;
}

DataBlock ASNLib::encodeReal(float val, bool tagCheck)
{
    Debug(s_libName.c_str(),DebugInfo,"::encodeReal() - STUB: encoding for real values not implemented");
    DataBlock data;
    return data;
}

DataBlock ASNLib::encodeString(String str, int type, bool tagCheck)
{
    DataBlock data;
    uint8_t tag = type;

    DataBlock contents;
    if (type == NUMERIC_STR ||
	type == PRINTABLE_STR ||
	type == IA5_STR ||
	type == VISIBLE_STR )
	contents.append(str);

    if (contents.length() == 0)
	return data;

    if (tagCheck) {
	data.append(&tag, 1);
	DataBlock len = buildLength(contents);
	data.append(len);
    }
    data.append(contents);
    XDebug(s_libName.c_str(),DebugAll,"::encodeString() - encoded string into %u bytes",data.length());
    return data;
}

DataBlock ASNLib::encodeUtf8(String str, bool tagCheck)
{
    DDebug(s_libName.c_str(),DebugAll,"::encodeUtf8()");
    DataBlock data;
    uint8_t tag = UTF8_STR;
    DataBlock contents;
    contents.append(str);
    if (tagCheck) {
	data.append(&tag, 1);
	DataBlock len = buildLength(contents);
	data.append(len);
    }
    data.append(contents);
    XDebug(s_libName.c_str(),DebugAll,"::encodeString() - encoded UTF8 string into %u bytes",data.length());
    return data;
}

DataBlock ASNLib::encodeGenTime(unsigned int time, unsigned int fractions, bool tagCheck)
{
    DataBlock data;
    uint8_t tag = GENERALIZED_TIME;

    int year;
    unsigned int month, day, hours, minutes, seconds;
    if (!Time::toDateTime(time, year, month, day, hours, minutes, seconds))
	return data;
    String dateTime = "";
    dateTime += year;
    (month < 10) ? dateTime += 0 : "";
    dateTime += month;
    (day < 10) ? dateTime += 0 : "";
    dateTime += day;
    (hours < 10) ? dateTime += 0 : "";
    dateTime += hours;
    (minutes < 10) ? dateTime += 0 : "";
    dateTime += minutes;
    (seconds < 10) ? dateTime += 0 : "";
    dateTime += seconds;
    if (fractions != 0) {
	dateTime += ".";
	dateTime += fractions;
    }
    dateTime += 'Z';
    DataBlock contents;
    contents.append(dateTime);
    if (tagCheck) {
	data.append(&tag, 1);
	DataBlock len = buildLength(contents);
	data.append(len);
    }
    data.append(contents);
    XDebug(s_libName.c_str(),DebugAll,"::encodeGenTime(time='%u', fractions='%u') - encoded time value into %u bytes",time,fractions,data.length());
    return data;
}

DataBlock ASNLib::encodeUTCTime(unsigned int time, bool tagCheck)
{
    DataBlock data;
    uint8_t tag = UTC_TIME;

    int year;
    unsigned int month, day, hours, minutes, seconds;
    if (!Time::toDateTime(time, year, month, day, hours, minutes, seconds))
	return data;
    String dateTime = "";
    (year % 100 < 10) ?  dateTime += 0 : "";
    dateTime += (year % 100);
    (month < 10) ? dateTime += 0 : "";
    dateTime += month;
    (day < 10) ? dateTime += 0 : "";
    dateTime += day;
    (hours < 10) ? dateTime += 0 : "";
    dateTime += hours;
    (minutes < 10) ? dateTime += 0 : "";
    dateTime += minutes;
    (seconds < 10) ? dateTime += 0 : "";
    dateTime += seconds;

    dateTime += 'Z';

    DataBlock contents;
    contents.append(dateTime);
    if (tagCheck) {
	data.append(&tag, 1);
	DataBlock len = buildLength(contents);
	data.append(len);
    }
    data.append(contents);
    XDebug(s_libName.c_str(),DebugAll,"::encodeUTCTime(time='%u') - encoded time value into %u bytes",time,data.length());
    return data;
}

DataBlock ASNLib::encodeAny(DataBlock data, bool tagCheck)
{
    XDebug(s_libName.c_str(),DebugAll,"::encodeAny()");
    DataBlock db;
    db.append(data);
    return db;
}

int ASNLib::encodeSequence(DataBlock& data, bool tagCheck)
{
    DataBlock len;
    if (tagCheck) {
	len = buildLength(data);
	data.insert(len);
	DataBlock db;
	u_int8_t tag = SEQUENCE;
	db.append(&tag, 1);
	data.insert(db);
    }
    XDebug(s_libName.c_str(),DebugAll,"::encodeSequence() - added sequence tag and length for a block of %d bytes",data.length());
    return len.length();
}

int ASNLib::encodeSet(DataBlock& data, bool tagCheck)
{
    DDebug(s_libName.c_str(),DebugAll,"::encodeSet()");
    DataBlock len;
    if (tagCheck) {
	len = buildLength(data);
	data.insert(len);
	DataBlock db;
	u_int8_t tag = SET;
	db.append(&tag, 1);
	data.insert(db);
    }
    XDebug(s_libName.c_str(),DebugAll,"::encodeSet() - added set tag and length for a block of %d bytes",data.length());
    return len.length();
}

/**
  * AsnTag
  */
void AsnTag::decode(AsnTag& tag, DataBlock& data)
{
    XDebug(s_libName.c_str(),DebugAll,"AsnTag::decode()");
    tag.classType((Class)(data[0] & 0xc0));
    tag.type((Type)(data[0] & 0x20));

    unsigned int code = 0;
    code |= data[0] & 0x1f;
    unsigned int len = 1;
    if (IS_EXTENSION_ID(code) && data.length() >= 2) { // extended tag
	code = 0;
	while (len < data.length() && (data[len] & ASN_BIT8) == ASN_BIT8) {
	    code = code << 8;
	    code |= (data[len] & 0x7f);
	    len++;
	}
	code |= data[len] & 0x7f;
    }
    tag.code(code);
    tag.encode();
}

void AsnTag::encode(Class clas, Type type, unsigned int code, DataBlock& data)
{
    XDebug(s_libName.c_str(),DebugAll,"AsnTag::encode(clas=0x%x, type=0x%x, code=%u)",clas,type,code);
    if (code < 31) {
	u_int8_t tag = clas | type | code;
	data.insert(DataBlock(&tag,sizeof(tag)));
    }
    else {
	u_int8_t last = clas | type | 31;
	DataBlock coding;
 	coding.append(&last,sizeof(last));
	int size = sizeof(unsigned int);
	bool start = false;
	while (size > 1) {
	    u_int8_t msb = (code >> ((size - 1) * 8));
	    if (start) {
		msb |= 0x80;
		coding.append(&msb,sizeof(msb));
	    }
	    else {
		if (msb == 0) {
		    size--;
		    continue;
		}
		else {
		    start = true;
		    msb |= 0x80;
		    coding.append(&msb,sizeof(msb));
		}
	    }
	    size--;
	}
	last = code;
	coding.append(&last,sizeof(last));
	data.insert(coding);
    }
#ifdef XDEBUG
    String str;
    str.hexify(data.data(),data.length(),' ');
    Debug(s_libName.c_str(),DebugAll,"AsnTag::encode(clas=0x%x, type=0x%x, code=%u) tag=%s",clas,type,code,str.c_str());
#endif
}

/**
  * ASNObjId
  */
ASNObjId::ASNObjId()
{
}

ASNObjId::ASNObjId(const String& val)
    : m_value(val)
{
    DDebug(s_libName.c_str(),DebugAll,"ASNObjId('%s') created",val.c_str());
}

ASNObjId::ASNObjId(const String& name, const String& val)
    : m_value(val), m_name(name)
{
    DDebug(s_libName.c_str(),DebugAll,"ASNObjId('%s', '%s') created",name.c_str(),val.c_str());
}

ASNObjId::ASNObjId(AsnMib* mib)
{
    DDebug(s_libName.c_str(),DebugAll,"ASNObjId() created from AsnMib [%p]",mib);
    if (mib) {
	m_name = mib->getName();
	m_value = mib->getOID();
    }
}

ASNObjId::~ASNObjId()
{
    m_ids.clear();
}

void ASNObjId::toDataBlock()
{
    DDebug(s_libName.c_str(),DebugAll,"ASNObjId::toDataBlock() '%s'", m_value.c_str());
    m_ids.clear();
    ObjList* list = m_value.split('.',false);
    if (list) {
    	for (ObjList* o = list->skipNull(); o; o = o->skipNext()) {
    	    String* s = static_cast<String*>(o->get());
    	    int val = s->toInteger();
    	    if (val < 128)
	    	m_ids.append(&val, 1);
	    else {
	        DataBlock db;
	        int size = sizeof(int);
	        uint8_t v = val;
	        v = v & 0x7f;
	        db.append(&v,1);
	        size--;
	        val = val >> 7;
	        while (val != 0) {
		    v = val;
		    v = v & 0x7f;
		    v = v | 0x80;
		    DataBlock tmp;
		    tmp.append(&v,1);
		    db.insert(tmp);
		    size--;
		    val = val >> 7;
	    	}
	    	m_ids.append(db);
    	    }
	}
	TelEngine::destruct(list);
    }
}

DataBlock ASNObjId::getIds()
{
    toDataBlock();
    return m_ids;
}

/**
  * AsnMib
  */
TokenDict AsnMib::s_access[] = {
    {"accessible-for-notify",	AsnMib::accessibleForNotify},
    {"read-only",		AsnMib::readOnly},
    {"read-write", 		AsnMib::readWrite},
    {"read-create",		AsnMib::readCreate},
    {0,0}
};

AsnMib::AsnMib(NamedList& params)
{
    if (!params)
	return;
    m_index = 0;
    m_oid = params;
    m_name = params.getValue("name","");
    m_access = params.getValue("access","");
    m_accessVal = lookup(m_access,s_access,0);
    m_type = params.getValue("type","");
    m_revision = params.getValue("revision","");
    XDebug(s_libName.c_str(),DebugAll,"new AsnMib created with oid : '%s', access : '%s', type : '%s'",
	    m_oid.c_str(),m_access.c_str(),m_type.c_str());
}

int AsnMib::compareTo(AsnMib* mib)
{
    if (!mib)
	return 1;
    DDebug(s_libName,DebugInfo,"AsnMib::compareTo('%s'='%s' [%p]) this=%s[%s] [%p]",
    		mib->getName().c_str(),mib->toString().c_str(),mib,getName().c_str(),toString().c_str(),this);

    // they're equal
    if (toString() == mib->toString())
    	return 0;

    ObjList* myIDs = toString().split('.',false);
    ObjList* mibIDs = mib->toString().split('.',false);

    ObjList* o1 = myIDs->skipNull();
    ObjList* o2 = mibIDs->skipNull();
    while (o1 && o2) {
    	String* str1 = static_cast<String*>(o1->get());
    	o1 = o1->skipNext();
    	String* str2 = static_cast<String*>(o2->get());
    	o2 = o2->skipNext();
    	int diff = str1->toInteger() - str2->toInteger();
    	if (diff == 0)
    	    continue;
    	if (diff > 0) {
    	    TelEngine::destruct(myIDs);
    	    TelEngine::destruct(mibIDs);
    	    return 1;
    	}
    	if (diff < 0) {
    	    TelEngine::destruct(myIDs);
    	    TelEngine::destruct(mibIDs);
    	    return -1;
    	}
    }

    int retValue = 0;
    if (!o1)
    	retValue = -1;
    else if (!o2)
    	retValue = 1;
    TelEngine::destruct(myIDs);
    TelEngine::destruct(mibIDs);
    return retValue;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
