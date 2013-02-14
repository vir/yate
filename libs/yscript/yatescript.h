/*
 * yatescript.h
 * Yet Another (Java)script library
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2011 Null Team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef __YATESCRIPT_H
#define __YATESCRIPT_H

#include <yateclass.h>

#ifdef _WINDOWS

#ifdef LIBYSCRIPT_EXPORTS
#define YSCRIPT_API __declspec(dllexport)
#else
#ifndef LIBYSCRIPT_STATIC
#define YSCRIPT_API __declspec(dllimport)
#endif
#endif

#endif /* _WINDOWS */

#ifndef YSCRIPT_API
#define YSCRIPT_API
#endif

/**
 * Holds all Telephony Engine related classes.
 */
namespace TelEngine {

class ExpEvaluator;
class ExpOperation;

/**
 * This class allows extending ExpEvaluator to implement custom fields and functions
 * @short ExpEvaluator extending interface
 */
class YSCRIPT_API ExpExtender
{
public:
    /**
     * Destructor
     */
    virtual ~ExpExtender()
	{ }

    /**
     * Retrieve the reference counted object owning this interface
     * @return Pointer to object owning the extender, NULL if no ownership
     */
    virtual RefObject* refObj();

    /**
     * Check if a certain field is assigned in extender
     * @param stack Evaluation stack in use
     * @param name Name of the field to test
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return True if the field is present
     */
    virtual bool hasField(ObjList& stack, const String& name, GenObject* context) const;

    /**
     * Get a pointer to a field in extender
     * @param stack Evaluation stack in use
     * @param name Name of the field to retrieve
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return Pointer to field, NULL if not present
     */
    virtual NamedString* getField(ObjList& stack, const String& name, GenObject* context) const;

    /**
     * Try to evaluate a single function
     * @param stack Evaluation stack in use, parameters are popped off this stack
     *  and results are pushed back on stack
     * @param oper Function to evaluate
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return True if evaluation succeeded
     */
    virtual bool runFunction(ObjList& stack, const ExpOperation& oper, GenObject* context);

    /**
     * Try to evaluate a single field
     * @param stack Evaluation stack in use, field value must be pushed on it
     * @param oper Field to evaluate
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return True if evaluation succeeded
     */
    virtual bool runField(ObjList& stack, const ExpOperation& oper, GenObject* context);

    /**
     * Try to assign a value to a single field
     * @param stack Evaluation stack in use
     * @param oper Field to assign to, contains the field name and new value
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return True if assignment succeeded
     */
    virtual bool runAssign(ObjList& stack, const ExpOperation& oper, GenObject* context);
};

/**
 * A class used to build stack based (posifix) expression parsers and evaluators
 * @short An expression parser and evaluator
 */
class YSCRIPT_API ExpEvaluator : public DebugEnabler
{
public:
    /**
     * Parsing styles
     */
    enum Parser {
	C,
	SQL,
    };

    /**
     * Operation codes
     */
    enum Opcode {
	// FORTH style notation of effect on stack, C-syntax expression
	OpcNone = 0,// ( --- )
	OpcNull,    // ( --- A)
	OpcPush,    // ( --- A)
	OpcDrop,    // (A --- )
	OpcDup,     // (A --- A A)
	OpcSwap,    // (A B --- B A)
	OpcRot,     // (A B C --- B C A)
	OpcOver,    // (A B --- A B A)
	// Arithmetic operators
	OpcAdd,     // (A B --- A+B)
	OpcSub,     // (A B --- A-B)
	OpcMul,     // (A B --- A*B)
	OpcDiv,     // (A B --- A/B)
	OpcMod,     // (A B --- A%B)
	OpcNeg,     // (A --- -A)
	OpcIncPre,  // (A --- ++A)
	OpcDecPre,  // (A --- --A)
	OpcIncPost, // (A --- A++)
	OpcDecPost, // (A --- A--)
	// Bitwise logic operators
	OpcAnd,     // (A B --- A&B)
	OpcOr,      // (A B --- A|B)
	OpcXor,     // (A B --- A^B)
	OpcNot,     // (A --- ~A)
	OpcShl,     // (A B --- A<<B)
	OpcShr,     // (A B --- A>>B)
	// Boolean logic operators
	OpcLAnd,    // (A B --- A&&B)
	OpcLOr,     // (A B --- A||B)
	OpcLXor,    // (A B --- A^^B)
	OpcLNot,    // (A --- !A)
	// String concatenation
	OpcCat,     // (A B --- A.B)
	// String matching
	OpcReM,     // (A B --- Amatch/B/)
	OpcReIM,    // (A B --- Amatch_insensitive/B/)
	OpcReNm,    // (A B --- A!match/B/)
	OpcReINm,   // (A B --- A!match_insensitive/B/)
	OpcLike,    // (A B --- AlikeB)
	OpcILike,   // (A B --- Alike_insensitiveB)
	OpcNLike,   // (A B --- A!likeB)
	OpcNIlike,  // (A B --- A!like_insensitiveB)
	// Comparation operators
	OpcEq,      // (A B --- A==B)
	OpcNe,      // (A B --- A!=B)
	OpcGt,      // (A B --- A>B)
	OpcLt,      // (A B --- A<B)
	OpcGe,      // (A B --- A>=B)
	OpcLe,      // (A B --- A<=B)
	// Ternary conditional operator
	OpcCond,    // (A B C --- A?B:C)
	// Field naming operator
	OpcAs,      // (A B --- A[name=B])
	// Field replacement
	OpcField,   // (A --- A)
	// Call of function with N parameters
	OpcFunc,    // (... funcN --- func(...))
	// Label for a jump
	OpcLabel,   // ( --- )
	// Push with deep copy
	OpcCopy,    // ( --- CopiedA)
	// Field assignment - can be ORed with other binary operators
	OpcAssign  = 0x0100, // (A B --- B,(&A=B))
	// Private extension area for derived classes
	OpcPrivate = 0x1000
    };

    /**
     * Constructs an evaluator from an operator dictionary
     * @param operators Pointer to operator dictionary, longest strings first
     * @param unaryOps Pointer to unary operators dictionary, longest strings first
     */
    explicit ExpEvaluator(const TokenDict* operators = 0, const TokenDict* unaryOps = 0);

    /**
     * Constructs an evaluator from a parser style
     * @param style Style of parsing to use
     */
    explicit ExpEvaluator(Parser style);

    /**
     * Copy constructor
     * @param original Evaluator to copy the operation list from
     */
    ExpEvaluator(const ExpEvaluator& original);

    /**
     * Destructor
     */
    virtual ~ExpEvaluator();

    /**
     * Parse and compile an expression
     * @param expr Pointer to expression to compile
     * @param context Pointer to arbitrary object to be passed to called methods
     * @return Number of expressions compiled, zero on error
     */
    int compile(const char* expr, GenObject* context = 0);

    /**
     * Evaluate the expression, optionally return results
     * @param results List to fill with results row
     * @param context Pointer to arbitrary object to be passed to called methods
     * @return True if expression evaluation succeeded, false on failure
     */
    bool evaluate(ObjList* results, GenObject* context = 0) const;

    /**
     * Evaluate the expression, return computed results
     * @param results List to fill with results row
     * @param context Pointer to arbitrary object to be passed to called methods
     * @return True if expression evaluation succeeded, false on failure
     */
    inline bool evaluate(ObjList& results, GenObject* context = 0) const
	{ return evaluate(&results,context); }

    /**
     * Evaluate the expression, return computed results
     * @param results List of parameters to populate with results row
     * @param index Index of result row, zero to not include an index
     * @param prefix Prefix to prepend to parameter names
     * @param context Pointer to arbitrary object to be passed to called methods
     * @return Number of result columns, -1 on failure
     */
    int evaluate(NamedList& results, unsigned int index = 0, const char* prefix = 0, GenObject* context = 0) const;

    /**
     * Evaluate the expression, return computed results
     * @param results Array of result rows to populate
     * @param index Index of result row, zero to just set column headers
     * @param context Pointer to arbitrary object to be passed to called methods
     * @return Number of result columns, -1 on failure
     */
    int evaluate(Array& results, unsigned int index, GenObject* context = 0) const;

    /**
     * Simplify the expression, performs constant folding
     * @return True if the expression was simplified
     */
    inline bool simplify()
	{ return trySimplify(); }

    /**
     * Check if a parse or compile error was encountered
     * @return True if the evaluator encountered an error
     */
    inline bool inError() const
	{ return m_inError; }

    /**
     * Retrieve the number of line currently being parsed
     * @return Number of current parsed line, 1 is the first line
     */
    inline unsigned int lineNumber() const
	{ return m_lineNo; }

    /**
     * Check if the expression is empty (no operands or operators)
     * @return True if the expression is completely empty
     */
    virtual bool null() const;

    /**
     * Dump a list of operations according to current operators dictionary
     * @param codes List of operation codes
     * @param res Result string representation of operations
     */
    void dump(const ObjList& codes, String& res) const;

    /**
     * Dump the postfix expression according to current operators dictionary
     * @param res Result string representation of operations
     */
    inline void dump(String& res) const
	{ return dump(m_opcodes,res); }

    /**
     * Dump a list of operations according to current operators dictionary
     * @param codes List of operation codes
     * @return String representation of operations
     */
    inline String dump(const ObjList& codes) const
	{ String s; dump(codes,s); return s; }

    /**
     * Dump the postfix expression according to current operators dictionary
     * @return String representation of operations
     */
    inline String dump() const
	{ String s; dump(s); return s; }

    /**
     * Retrieve the internally used operator dictionary
     * @return Pointer to operators dictionary in use
     */
    inline const TokenDict* operators() const
	{ return m_operators; }

    /**
     * Retrieve the internally used unary operators dictionary
     * @return Pointer to unary operators dictionary in use
     */
    inline const TokenDict* unaryOps() const
	{ return m_unaryOps; }

    /**
     * Retrieve the internally used expression extender
     * @return Pointer to the extender in use, NULL if none
     */
    inline ExpExtender* extender() const
	{ return m_extender; }

    /**
     * Set the expression extender to use in evaluation
     * @param ext Pointer to the extender to use, NULL to remove current
     */
    void extender(ExpExtender* ext);

    /**
     * Retrieve the line number from one to three operands
     * @param op1 First operand
     * @param op2 Optional second operand
     * @param op3 Optional third operand
     * @return Line number at compile time, zero if not found
     */
    static unsigned int getLineOf(ExpOperation* op1, ExpOperation* op2 = 0, ExpOperation* op3 = 0);

    /**
     * Push an operand on an evaluation stack
     * @param stack Evaluation stack to remove the operand from
     * @param oper Operation to push on stack, NULL will not be pushed
     */
    static void pushOne(ObjList& stack, ExpOperation* oper);

    /**
     * Pops an operand off an evaluation stack, does not pop a barrier
     * @param stack Evaluation stack to remove the operand from
     * @return Operator removed from stack, NULL if stack underflow
     */
    static ExpOperation* popOne(ObjList& stack);

    /**
     * Pops any operand (including barriers) off an evaluation stack
     * @param stack Evaluation stack to remove the operand from
     * @return Operator removed from stack, NULL if stack underflow
     */
    static ExpOperation* popAny(ObjList& stack);

    /**
     * Pops and evaluate the value of an operand off an evaluation stack, does not pop a barrier
     * @param stack Evaluation stack to remove the operand from
     * @param context Pointer to arbitrary object to be passed to called methods
     * @return Value removed from stack, NULL if stack underflow or field not evaluable
     */
    virtual ExpOperation* popValue(ObjList& stack, GenObject* context = 0) const;

    /**
     * Try to evaluate a single operation
     * @param stack Evaluation stack in use, operands are popped off this stack
     *  and results are pushed back on stack
     * @param oper Operation to execute
     * @param context Pointer to arbitrary object to be passed to called methods
     * @return True if evaluation succeeded
     */
    virtual bool runOperation(ObjList& stack, const ExpOperation& oper, GenObject* context = 0) const;

    /**
     * Convert all fields on the evaluation stack to their values
     * @param stack Evaluation stack to evaluate fields from
     * @param context Pointer to arbitrary object to be passed to called methods
     * @return True if all fields on the stack were evaluated properly
     */
    virtual bool runAllFields(ObjList& stack, GenObject* context = 0) const;

protected:
    /**
     * Method to skip over whitespaces, count parsed lines too
     * @param expr Pointer to expression cursor, gets advanced
     * @return First character after whitespaces where expr points
     */
    virtual char skipWhites(const char*& expr);

    /**
     * Helper method to conditionally convert to lower case
     * @param chr Character to convert
     * @param makeLower True to convert chr to lower case
     * @return Converted character or original if conversion not requested
     */
    inline static char condLower(char chr, bool makeLower)
	{ return (makeLower && ('A' <= chr) && (chr <= 'Z')) ? (chr + ('a' - 'A')) : chr; }

    /**
     * Helper method to return next operator in the parsed text
     * @param expr Pointer to text to parse, gets advanced if succeeds
     * @param operators Pointer to operators table to use
     * @param caseInsensitive Match case-insensitive if set
     * @return Operator code, OpcNone on failure
     */
    Opcode getOperator(const char*& expr, const TokenDict* operators, bool caseInsensitive = false) const;

    /**
     * Check if a character can be part of a keyword or identifier
     * @param c Character to check
     * @return True if the character can be part of a keyword or identifier
     */
    virtual bool keywordChar(char c) const;

    /**
     * Helper method to count characters making a keyword
     * @param str Pointer to text without whitespaces in front
     * @return Length of the keyword, 0 if a valid keyword doesn't follow
     */
    virtual int getKeyword(const char* str) const;

    /**
     * Helper method to display debugging errors internally
     * @param error Text of the error
     * @param text Optional text that caused the error
     * @param line Number of line generating the error, zero for parsing errors
     * @return Always returns false
     */
    bool gotError(const char* error = 0, const char* text = 0, unsigned int line = 0) const;

    /**
     * Helper method to set error flag and display debugging errors internally
     * @param error Text of the error
     * @param text Optional text that caused the error
     * @param line Number of line generating the error, zero for parsing errors
     * @return Always returns false
     */
    bool gotError(const char* error = 0, const char* text = 0, unsigned int line = 0);

    /**
     * Helper method to display debugging errors internally
     * @param error Text of the error
     * @param line Number of line generating the error, zero for parsing errors
     * @return Always returns false
     */
    inline bool gotError(const char* error, unsigned int line) const
	{ return gotError(error, 0, line); }

    /**
     * Helper method to set error flag and display debugging errors internally
     * @param error Text of the error
     * @param line Number of line generating the error, zero for parsing errors
     * @return Always returns false
     */
    inline bool gotError(const char* error, unsigned int line)
	{ return gotError(error, 0, line); }

    /**
     * Formats a line number to display in error messages
     * @param buf String buffer used to return the value
     * @param line Line number to format
     */
    virtual void formatLineNo(String& buf, unsigned int line) const;

    /**
     * Runs the parser and compiler for one (sub)expression
     * @param expr Pointer to text to parse, gets advanced
     * @param stop Optional character expected after the expression
     * @param nested User defined object to pass for nested parsing
     * @return True if one expression was compiled and a separator follows
     */
    bool runCompile(const char*& expr, char stop, GenObject* nested = 0);

    /**
     * Runs the parser and compiler for one (sub)expression
     * @param expr Pointer to text to parse, gets advanced
     * @param stop Optional list of possible characters expected after the expression
     * @param nested User defined object to pass for nested parsing
     * @return True if one expression was compiled and a separator follows
     */
    virtual bool runCompile(const char*& expr, const char* stop = 0, GenObject* nested = 0);

    /**
     * Skip over comments and whitespaces
     * @param expr Pointer to expression cursor, gets advanced
     * @param context Pointer to arbitrary object to be passed to called methods
     * @return First character after comments or whitespaces where expr points
     */
    virtual char skipComments(const char*& expr, GenObject* context = 0);

    /**
     * Process top-level preprocessor directives
     * @param expr Pointer to expression cursor, gets advanced
     * @param context Pointer to arbitrary object to be passed to called methods
     * @return Number of expressions compiled, negative if no more directives
     */
    virtual int preProcess(const char*& expr, GenObject* context = 0);

    /**
     * Returns next operator in the parsed text
     * @param expr Pointer to text to parse, gets advanced if succeeds
     * @return Operator code, OpcNone on failure
     */
    virtual Opcode getOperator(const char*& expr);

    /**
     * Returns next unary operator in the parsed text
     * @param expr Pointer to text to parse, gets advanced if succeeds
     * @return Operator code, OpcNone on failure
     */
    virtual Opcode getUnaryOperator(const char*& expr);

    /**
     * Returns next unary postfix operator in the parsed text
     * @param expr Pointer to text to parse, gets advanced if succeeds
     * @param precedence The precedence of the previous operator
     * @return Operator code, OpcNone on failure
     */
    virtual Opcode getPostfixOperator(const char*& expr, int precedence = 0);

    /**
     * Helper method to get the canonical name of an operator
     * @param oper Operator code
     * @return name of the operator, NULL if it doesn't have one
     */
    virtual const char* getOperator(Opcode oper) const;

    /**
     * Get the precedence of an operator
     * @param oper Operator code
     * @return Precedence of the operator, zero (lowest) if unknown
     */
    virtual int getPrecedence(Opcode oper) const;

    /**
     * Get the associativity of an operator
     * @param oper Operator code
     * @return True if the operator is right-to-left associative, false if left-to-right
     */
    virtual bool getRightAssoc(Opcode oper) const;

    /**
     * Check if we are at an expression separator and optionally skip past it
     * @param expr Pointer to text to check, gets advanced if asked to remove separator
     * @param remove True to skip past the found separator
     * @return True if a separator was found
     */
    virtual bool getSeparator(const char*& expr, bool remove);

    /**
     * Get an instruction or block, advance parsing pointer past it
     * @param expr Pointer to text to parse, gets advanced on success
     * @param stop Optional character expected after the instruction
     * @param nested User defined object passed from nested parsing
     * @return True if succeeded, must add the operands internally
     */
    virtual bool getInstruction(const char*& expr, char stop = 0, GenObject* nested = 0);

    /**
     * Get an operand, advance parsing pointer past it
     * @param expr Pointer to text to parse, gets advanced on success
     * @param endOk Consider reaching the end of string a success
     * @param precedence The precedence of the previous operator
     * @return True if succeeded, must add the operand internally
     */
    virtual bool getOperand(const char*& expr, bool endOk = true, int precedence = 0);

    /**
     * Get an inline simple type, usually string or number
     * @param expr Pointer to text to parse, gets advanced on success
     * @param constOnly Return only inline constants
     * @return True if succeeded, must add the operand internally
     */
    virtual bool getSimple(const char*& expr, bool constOnly = false);

    /**
     * Get a numerical operand, advance parsing pointer past it
     * @param expr Pointer to text to parse, gets advanced on success
     * @return True if succeeded, must add the operand internally
     */
    virtual bool getNumber(const char*& expr);

    /**
     * Get a string operand, advance parsing pointer past it
     * @param expr Pointer to text to parse, gets advanced on success
     * @return True if succeeded, must add the operand internally
     */
    virtual bool getString(const char*& expr);

    /**
     * Get a function call, advance parsing pointer past it
     * @param expr Pointer to text to parse, gets advanced on success
     * @return True if succeeded, must add the operand internally
     */
    virtual bool getFunction(const char*& expr);

    /**
     * Helper method - get a string, advance parsing pointer past it
     * @param expr Pointer to string separator, gets advanced on success
     * @param str String in which the result is returned
     * @return True if succeeded
     */
    virtual bool getString(const char*& expr, String& str);

    /**
     * Helper method - get an escaped component of a string
     * @param expr Pointer past escape character, gets advanced on success
     * @param str String in which the result is returned
     * @param sep String separator character
     * @return True if succeeded
     */
    virtual bool getEscape(const char*& expr, String& str, char sep);

    /**
     * Get a field keyword, advance parsing pointer past it
     * @param expr Pointer to text to parse, gets advanced on success
     * @return True if succeeded, must add the operand internally
     */
    virtual bool getField(const char*& expr);

    /**
     * Add an aready built operation to the expression and set its line number
     * @param oper Operation to add
     * @param line Line number where operation was compiled, zero to used parsing point
     */
    void addOpcode(ExpOperation* oper, unsigned int line = 0);

    /**
     * Add a simple operator to the expression
     * @param oper Operator code to add
     * @param barrier True to create an evaluator stack barrier
     * @return Newly added operation
     */
    ExpOperation* addOpcode(Opcode oper, bool barrier = false);

    /**
     * Add a simple operator to the expression
     * @param oper Operator code to add
     * @param value Integer value to add
     * @param barrier True to create an evaluator stack barrier
     * @return Newly added operation
     */
    ExpOperation* addOpcode(Opcode oper, long int value, bool barrier = false);

    /**
     * Add a string constant to the expression
     * @param value String value to add, will be pushed on execution
     * @return Newly added operation
     */
    ExpOperation* addOpcode(const String& value);

    /**
     * Add an integer constant to the expression
     * @param value Integer value to add, will be pushed on execution
     * @return Newly added operation
     */
    ExpOperation* addOpcode(long int value);

    /**
     * Add a boolean constant to the expression
     * @param value Boolean value to add, will be pushed on execution
     * @return Newly added operation
     */
    ExpOperation* addOpcode(bool value);

    /**
     * Add a function or field to the expression
     * @param oper Operator code to add, must be OpcField or OpcFunc
     * @param name Name of the field or function, case sensitive
     * @param value Numerical value used as parameter count to functions
     * @param barrier True to create an exavuator stack barrier
     * @return Newly added operation
     */
    ExpOperation* addOpcode(Opcode oper, const String& name, long int value = 0, bool barrier = false);

    /**
     * Remove from the code and return the last operation
     * @return Operation removed from end of code, NULL if no operations remaining
     */
    ExpOperation* popOpcode();

    /**
     * Try to apply simplification to the expression
     * @return True if the expression was simplified
     */
    virtual bool trySimplify();

    /**
     * Try to evaluate a list of operation codes
     * @param opcodes List of operation codes to evaluate
     * @param stack Evaluation stack in use, results are left on stack
     * @param context Pointer to arbitrary object to be passed to called methods
     * @return True if evaluation succeeded
     */
    virtual bool runEvaluate(const ObjList& opcodes, ObjList& stack, GenObject* context = 0) const;

    /**
     * Try to evaluate a vector of operation codes
     * @param opcodes ObjVector of operation codes to evaluate
     * @param stack Evaluation stack in use, results are left on stack
     * @param context Pointer to arbitrary object to be passed to called methods
     * @param index Index in operation codes to start evaluation from
     * @return True if evaluation succeeded
     */
    virtual bool runEvaluate(const ObjVector& opcodes, ObjList& stack, GenObject* context = 0, unsigned int index = 0) const;

    /**
     * Try to evaluate the expression
     * @param stack Evaluation stack in use, results are left on stack
     * @param context Pointer to arbitrary object to be passed to called methods
     * @return True if evaluation succeeded
     */
    virtual bool runEvaluate(ObjList& stack, GenObject* context = 0) const;

    /**
     * Try to evaluate a single function
     * @param stack Evaluation stack in use, parameters are popped off this stack
     *  and results are pushed back on stack
     * @param oper Function to evaluate
     * @param context Pointer to arbitrary object to be passed to called methods
     * @return True if evaluation succeeded
     */
    virtual bool runFunction(ObjList& stack, const ExpOperation& oper, GenObject* context = 0) const;

    /**
     * Try to evaluate a single field
     * @param stack Evaluation stack in use, field value must be pushed on it
     * @param oper Field to evaluate
     * @param context Pointer to arbitrary object to be passed to called methods
     * @return True if evaluation succeeded
     */
    virtual bool runField(ObjList& stack, const ExpOperation& oper, GenObject* context = 0) const;

    /**
     * Try to assign a value to a single field
     * @param stack Evaluation stack in use
     * @param oper Field to assign to, contains the field name and new value
     * @param context Pointer to arbitrary object to be passed to called methods
     * @return True if assignment succeeded
     */
    virtual bool runAssign(ObjList& stack, const ExpOperation& oper, GenObject* context = 0) const;

    /**
     * Dump a single operation according to current operators dictionary
     * @param oper Operation to dump
     * @param res Result string representation of operations
     */
    virtual void dump(const ExpOperation& oper, String& res) const;

    /**
     * Internally used operator dictionary
     */
    const TokenDict* m_operators;

    /**
     * Internally used unary operators dictionary
     */
    const TokenDict* m_unaryOps;

    /**
     * Internally used list of operands and operator codes
     */
    ObjList m_opcodes;

    /**
     * Flag that we encountered a parse or compile error
     */
    bool m_inError;

    /**
     * Current line index
     */
    unsigned int m_lineNo;

private:
    bool getOperandInternal(const char*& expr, bool endOk, int precedence);
    ExpExtender* m_extender;
};

/**
 * This class describes a single operation in an expression evaluator
 * @short A single operation in an expression
 */
class YSCRIPT_API ExpOperation : public NamedString
{
    friend class ExpEvaluator;
    YCLASS(ExpOperation,NamedString)
public:
    /**
     * Special value that is not recognized as an integer value
     * @return A value that indicates a non-integer value
     */
    inline static long int nonInteger()
	{ return LONG_MIN; }

    /**
     * Copy constructor
     * @param original Operation to copy
     */
    inline ExpOperation(const ExpOperation& original)
	: NamedString(original.name(),original),
	  m_opcode(original.opcode()), m_number(original.number()),
	  m_lineNo(0), m_barrier(original.barrier())
	{ }

    /**
     * Copy constructor with renaming, to be used for named results
     * @param original Operation to copy
     * @param name Name of the newly created operation
     */
    inline ExpOperation(const ExpOperation& original, const char* name)
	: NamedString(name,original),
	  m_opcode(original.opcode()), m_number(original.number()),
	  m_lineNo(0), m_barrier(original.barrier())
	{ }

    /**
     * Push String constructor
     * @param value String constant to push on stack on execution
     * @param name Optional of the newly created constant
     * @param autoNum Automatically convert to number if possible
     */
    inline explicit ExpOperation(const String& value, const char* name = 0, bool autoNum = false)
	: NamedString(name,value),
	  m_opcode(ExpEvaluator::OpcPush),
	  m_number(autoNum ? value.toLong(nonInteger()) : nonInteger()),
	  m_lineNo(0), m_barrier(false)
	{ if (autoNum && value.isBoolean()) m_number = value.toBoolean() ? 1 : 0; }

    /**
     * Push literal string constructor
     * @param value String constant to push on stack on execution
     * @param name Optional of the newly created constant
     */
    inline explicit ExpOperation(const char* value, const char* name = 0)
	: NamedString(name,value),
	  m_opcode(ExpEvaluator::OpcPush), m_number(nonInteger()), m_lineNo(0), m_barrier(false)
	{ }

    /**
     * Push Number constructor
     * @param value Integer constant to push on stack on execution
     * @param name Optional of the newly created constant
     */
    inline explicit ExpOperation(long int value, const char* name = 0)
	: NamedString(name,"NaN"),
	  m_opcode(ExpEvaluator::OpcPush),
	  m_number(value), m_lineNo(0), m_barrier(false)
	{ if (value != nonInteger()) String::operator=((int)value); }

    /**
     * Push Boolean constructor
     * @param value Boolean constant to push on stack on execution
     * @param name Optional of the newly created constant
     */
    inline explicit ExpOperation(bool value, const char* name = 0)
	: NamedString(name,String::boolText(value)),
	  m_opcode(ExpEvaluator::OpcPush),
	  m_number(value ? 1 : 0), m_lineNo(0), m_barrier(false)
	{ }

    /**
     * Constructor from components
     * @param oper Operation code
     * @param name Optional name of the operation or result
     * @param value Optional integer constant used as function parameter count
     * @param barrier True if the operation is an expression barrier on the stack
     */
    inline ExpOperation(ExpEvaluator::Opcode oper, const char* name = 0, long int value = nonInteger(), bool barrier = false)
	: NamedString(name,""),
	  m_opcode(oper), m_number(value), m_lineNo(0), m_barrier(barrier)
	{ }

    /**
     * Constructor of non-integer operation from components
     * @param oper Operation code
     * @param name Name of the operation or result
     * @param value String value of operation
     * @param barrier True if the operation is an expression barrier on the stack
     */
    inline ExpOperation(ExpEvaluator::Opcode oper, const char* name, const char* value, bool barrier = false)
	: NamedString(name,value),
	  m_opcode(oper), m_number(nonInteger()), m_lineNo(0), m_barrier(barrier)
	{ }

    /**
     * Retrieve the code of this operation
     * @return Operation code as declared in the expression evaluator
     */
    inline ExpEvaluator::Opcode opcode() const
	{ return m_opcode; }

    /**
     * Check if an integer value is stored
     * @return True if an integer value is stored
     */
    inline bool isInteger() const
	{ return m_number != nonInteger(); }

    /**
     * Retrieve the number stored in this operation
     * @return Stored number
     */
    inline long int number() const
	{ return m_number; }

    /**
     * Check if this operation acts as an evaluator barrier on the stack
     * @return True if an expression should not pop this operation off the stack
     */
    inline bool barrier() const
	{ return m_barrier; }

    /**
     * Retrieve the line number where the operation was compiled from
     * @return Line number, zero if unknown
     */
    inline unsigned int lineNumber() const
	{ return m_lineNo; }

    /**
     * Set the line number where the operation was compiled from
     * @param line Number of the compiled line
     */
    inline void lineNumber(unsigned int line)
	{ m_lineNo = line; }

    /**
     * Number assignment operator
     * @param num Numeric value to assign to the operation
     * @return Assigned number
     */
    inline long int operator=(long int num)
	{ m_number = num; String::operator=((int)num); return num; }

    /**
     * Retrieve the numeric value of the operation
     * @return Number contained in operation, zero if not a number
     */
    virtual long int valInteger() const;

    /**
     * Retrieve the boolean value of the operation
     * @return True if the operation is to be interpreted as true value
     */
    virtual bool valBoolean() const;

    /**
     * Clone and rename method
     * @param name Name of the cloned operation
     * @return New operation instance
     */
    virtual ExpOperation* clone(const char* name) const;

    /**
     * Clone method
     * @return New operation instance, may keep a reference to the old instance
     */
    inline ExpOperation* clone() const
	{ return clone(name()); }

    /**
     * Deep copy method
     * @param mtx Pointer to the mutex that serializes the copied object
     * @return New operation instance
     */
    virtual ExpOperation* copy(Mutex* mtx) const
	{ return clone(); }

private:
    ExpEvaluator::Opcode m_opcode;
    long int m_number;
    unsigned int m_lineNo;
    bool m_barrier;
};

/**
 * Small helper class that simplifies declaring native functions
 * @short Helper class to declare a native function
 */
class YSCRIPT_API ExpFunction : public ExpOperation
{
    YCLASS(ExpFunction,ExpOperation)
public:
    /**
     * Constructor
     * @param name Name of the function
     * @param argc Number of arguments expected by function
     */
    inline ExpFunction(const char* name, long int argc = 0)
	: ExpOperation(ExpEvaluator::OpcFunc,name,argc)
	{ if (name) (*this) << "[function " << name << "()]"; }

    /**
     * Retrieve the boolean value of the function (not of its result)
     * @return Always true
     */
    virtual bool valBoolean() const
	{ return true; }

    /**
     * Clone and rename method
     * @param name Name of the cloned operation
     * @return New operation instance
     */
    virtual ExpOperation* clone(const char* name) const;
};

/**
 * Helper class that allows wrapping entire objects in an operation
 * @short Object wrapper for evaluation
 */
class YSCRIPT_API ExpWrapper : public ExpOperation
{
public:
    /**
     * Constructor
     * @param object Pointer to the object to wrap
     * @param name Optional name of the wrapper
     * @param barrier True if the operation is an expression barrier on the stack
     */
    inline ExpWrapper(GenObject* object, const char* name = 0, bool barrier = false)
	: ExpOperation(ExpEvaluator::OpcPush,name,
	    object ? object->toString().c_str() : (const char*)0,barrier),
	  m_object(object)
	{ }

    /**
     * Constructor with special operation
     * @param opcode Operation code of the wrapper
     * @param object Pointer to the object to wrap
     */
    inline ExpWrapper(ExpEvaluator::Opcode opcode, GenObject* object)
	: ExpOperation(opcode,0,object ? object->toString().c_str() : (const char*)0,false),
	  m_object(object)
	{ }

    /**
     * Destructor, deletes the held object
     */
    virtual ~ExpWrapper()
	{ TelEngine::destruct(m_object); }

    /**
     * Get a pointer to a derived class given that class name
     * @param name Name of the class we are asking for
     * @return Pointer to the requested class or NULL if this object doesn't implement it
     */
    virtual void* getObject(const String& name) const;

    /**
     * Retrieve the boolean value of the operation
     * @return True if the wrapped object is to be interpreted as true value
     */
    virtual bool valBoolean() const;

    /**
     * Clone and rename method
     * @param name Name of the cloned operation
     * @return New operation instance
     */
    virtual ExpOperation* clone(const char* name) const;

    /**
     * Deep copy method
     * @param mtx Pointer to the mutex that serializes the copied object
     * @return New operation instance
     */
    virtual ExpOperation* copy(Mutex* mtx) const;

    /**
     * Object access method
     * @return Pointer to the held object
     */
    GenObject* object() const
	{ return m_object; }

private:
    GenObject* m_object;
};

/**
 * An evaluator for multi-row (tables like in SQL) expressions
 * @short An SQL-like table evaluator
 */
class YSCRIPT_API TableEvaluator
{
public:
    /**
     * Copy constructor, duplicates current state of original
     * @param original Evaluator to copy
     */
    TableEvaluator(const TableEvaluator& original);

    /**
     * Constructor from a parser synatx style
     * @param style Style of evaluator to create
     */
    TableEvaluator(ExpEvaluator::Parser style);

    /**
     * Constructor from operator description table
     * @param operators Pointer to operators synatx table
     * @param unaryOps Pointer to unary operators dictionary
     */
    TableEvaluator(const TokenDict* operators, const TokenDict* unaryOps);

    /**
     * Destructor
     */
    virtual ~TableEvaluator();

    /**
     * Evaluate the WHERE (selector) expression
     * @param context Pointer to arbitrary object to be passed to called methods
     * @return True if the current row is part of selection
     */
    virtual bool evalWhere(GenObject* context = 0);

    /**
     * Evaluate the SELECT (results) expression
     * @param results List to fill with results row
     * @param context Pointer to arbitrary object to be passed to called methods
     * @return True if evaluation succeeded
     */
    virtual bool evalSelect(ObjList& results, GenObject* context = 0);

    /**
     * Evaluate the LIMIT expression and cache the result
     * @param context Pointer to arbitrary object to be passed to called methods
     * @return Desired maximum number or result rows
     */
    virtual unsigned int evalLimit(GenObject* context = 0);

    /**
     * Set the expression extender to use in all evaluators
     * @param ext Pointer to the extender to use, NULL to remove current
     */
    void extender(ExpExtender* ext);

protected:
    ExpEvaluator m_select;
    ExpEvaluator m_where;
    ExpEvaluator m_limit;
    unsigned int m_limitVal;
};

class ScriptRun;

/**
 * A script execution context, holds global variables and objects
 * @short Script execution context
 */
class YSCRIPT_API ScriptContext : public RefObject, public ExpExtender
{
public:
    /**
     * Constructor
     * @param name Name of the context
     */
    inline explicit ScriptContext(const char* name = 0)
	: m_params(name)
	{ }

    /**
     * Access to the NamedList operator
     * @return Reference to the internal named list
     */
    inline NamedList& params()
	{ return m_params; }

    /**
     * Const access to the NamedList operator
     * @return Reference to the internal named list
     */
    inline const NamedList& params() const
	{ return m_params; }

    /**
     * Access any native NamedList hold by the context
     * @return Pointer to a native named list
     */
    virtual NamedList* nativeParams() const
	{ return 0; }

    /**
     * Override GenObject's method to return the internal name of the named list
     * @return A reference to the context name
     */
    virtual const String& toString() const
	{ return m_params; }

    /**
     * Get a pointer to a derived class given that class name
     * @param name Name of the class we are asking for
     * @return Pointer to the requested class or NULL if this object doesn't implement it
     */
    virtual void* getObject(const String& name) const;

    /**
     * Retrieve the reference counted object owning this interface
     * @return Pointer to this script context
     */
    virtual RefObject* refObj()
	{ return this; }

    /**
     * Retrieve the Mutex object used to serialize object access, if any
     * @return Pointer to the mutex or NULL if none applies
     */
    virtual Mutex* mutex() = 0;

    /**
     * Check if a certain field is assigned in context
     * @param stack Evaluation stack in use
     * @param name Name of the field to test
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return True if the field is present
     */
    virtual bool hasField(ObjList& stack, const String& name, GenObject* context) const;

    /**
     * Get a pointer to a field in the context
     * @param stack Evaluation stack in use
     * @param name Name of the field to retrieve
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return Pointer to field, NULL if not present
     */
    virtual NamedString* getField(ObjList& stack, const String& name, GenObject* context) const;

    /**
     * Fill a list with the unique names of all fields
     * @param names List to which key names must be added
     */
    virtual void fillFieldNames(ObjList& names);

    /**
     * Fill a list with the unique names of all fields
     * @param names List to which key names must be added
     * @param list List of parameters whose names to be added
     * @param skip Parameters starting with this prefix will not be added
     */
    static void fillFieldNames(ObjList& names, const NamedList& list, const char* skip = 0);

    /**
     * Try to evaluate a single function in the context
     * @param stack Evaluation stack in use, parameters are popped off this stack and results are pushed back on stack
     * @param oper Function to evaluate
     * @param context Pointer to context data passed from evaluation methods
     * @return True if evaluation succeeded
     */
    virtual bool runFunction(ObjList& stack, const ExpOperation& oper, GenObject* context);

    /**
     * Try to evaluate a single field in the context
     * @param stack Evaluation stack in use, field value must be pushed on it
     * @param oper Field to evaluate
     * @param context Pointer to context data passed from evaluation methods
     * @return True if evaluation succeeded
     */
    virtual bool runField(ObjList& stack, const ExpOperation& oper, GenObject* context);

    /**
     * Try to assign a value to a single field
     * @param stack Evaluation stack in use
     * @param oper Field to assign to, contains the field name and new value
     * @param context Pointer to context data passed from evaluation methods
     * @return True if assignment succeeded
     */
    virtual bool runAssign(ObjList& stack, const ExpOperation& oper, GenObject* context);

    /**
     * Copy all fields from another context
     * @param stack Evaluation stack in use
     * @param original Script context to copy from
     * @param context Pointer to context data passed from evaluation methods
     * @return True if all fields were copied
     */
    virtual bool copyFields(ObjList& stack, const ScriptContext& original, GenObject* context);

    /**
     * Try to evaluate a single field searching for a matching context
     * @param stack Evaluation stack in use, field value must be pushed on it
     * @param oper Field to evaluate
     * @param context Pointer to context data passed from evaluation methods
     * @return True if evaluation succeeded
     */
    bool runMatchingField(ObjList& stack, const ExpOperation& oper, GenObject* context);

private:
    NamedList m_params;
};

/**
 * Preparsed script code fragment ready to be executed
 * @short Script parsed code
 */
class YSCRIPT_API ScriptCode : public RefObject
{
    YCLASS(ScriptCode,RefObject)
public:
    /**
     * Context initializer for language specific globals
     * @param context Pointer to the context to initialize
     * @return True if context was properly populated with globals
     */
    virtual bool initialize(ScriptContext* context) const = 0;

    /**
     * Evaluation of a single code expression
     * @param runner Reference to the runtime to use in evaluation
     * @param results List to fill with expression results
     */
    virtual bool evaluate(ScriptRun& runner, ObjList& results) const = 0;

    /**
     * Create a runner adequate for this block of parsed code
     * @param context Script context, must not be NULL
     * @return A new script runner, NULL if context is NULL or feature is not supported
     */
    virtual ScriptRun* createRunner(ScriptContext* context)
	{ return 0; }
};

/**
 * A stack for a script running instance
 * @short Script runtime stack
 */
class YSCRIPT_API ScriptStack : public ObjList
{
    YCLASS(ScriptStack,ObjList)
    YNOCOPY(ScriptStack);
public:
    /**
     * Constructor
     * @param owner The script running instance that will own this stack
     */
    ScriptStack(ScriptRun* owner)
	: m_runner(owner)
	{ }

    /**
     * Retrieve the script running instance that owns this stack
     * @return Pointer to owner script instance
     */
    inline ScriptRun* runner()
	{ return m_runner; }

private:
    ScriptRun* m_runner;
};

/**
 * Operation that is to be executed by the script runtime before current operation
 * @short Asynchronous execution support
 */
class YSCRIPT_API ScriptAsync : public GenObject
{
    YCLASS(ScriptAsync,GenObject)
public:
    /**
     * Constructor
     * @param owner The script running instance that will own this operation
     */
    ScriptAsync(ScriptRun* owner)
	: m_runner(owner)
	{ }

    /**
     * Destructor
     */
    virtual ~ScriptAsync()
	{ }

    /**
     * Retrieve the script running instance that owns this stack
     * @return Pointer to owner script instance
     */
    inline ScriptRun* runner()
	{ return m_runner; }

    /**
     * Execute the aynchronous operation with context unlocked if the script is paused
     * @return True if the operation should be removed (was one-shot)
     */
    virtual bool run() = 0;

private:
    ScriptRun* m_runner;
};

/**
 * An instance of script code and data, status machine run by a single thread at a time
 * @short Script runtime execution
 */
class YSCRIPT_API ScriptRun : public GenObject, public Mutex
{
    friend class ScriptCode;
    YCLASS(ScriptRun,GenObject)
    YNOCOPY(ScriptRun);
public:
    /**
     * Runtime states
     */
    enum Status {
	Invalid,
	Running,
	Incomplete,
	Succeeded,
	Failed,
    };

    /**
     * Constructor
     * @param code Code fragment to execute
     * @param context Script context, an empty one will be allocated if NULL
     */
    ScriptRun(ScriptCode* code, ScriptContext* context = 0);

    /**
     * Destructor, disposes the code and context
     */
    virtual ~ScriptRun();

    /**
     * Retrieve the parsed code being executed
     * @return Pointer to ScriptCode object
     */
    inline ScriptCode* code() const
	{ return m_code; }

    /**
     * Retrieve the execution context associated with the runtime
     * @return Pointer to ScriptContext object
     */
    inline ScriptContext* context() const
	{ return m_context; }

    /**
     * Current state of the runtime
     */
    inline Status state() const
	{ return m_state; }

    /**
     * Get the text description of a runtime state
     * @param state State to describe
     * @return Description of the runtime state
     */
    static const char* textState(Status state);

    /**
     * Get the text description of the current runtime state
     * @return Description of the runtime state
     */
    inline const char* textState() const
	{ return textState(m_state); }

    /**
     * Access the runtime execution stack
     * @return The internal execution stack
     */
    inline ObjList& stack()
	{ return m_stack; }

    /**
     * Const access the runtime execution stack
     * @return The internal execution stack
     */
    inline const ObjList& stack() const
	{ return m_stack; }

    /**
     * Create a duplicate of the runtime with its own stack and state
     * @return New clone of the runtime
     */
    inline ScriptRun* clone() const
	{ return new ScriptRun(code(),context()); }

    /**
     * Resets code execution to the beginning, does not clear context
     * @param init Initialize context
     * @return Status of the runtime after reset
     */
    virtual Status reset(bool init = false);

    /**
     * Execute script from where it was left, may stop and return Incomplete state
     * @return Status of the runtime after code execution
     */
    virtual Status execute();

    /**
     * Execute script from the beginning until it returns a final state
     * @param init Initialize context
     * @return Final status of the runtime after code execution
     */
    virtual Status run(bool init = true);

    /**
     * Pause the script, make it return Incomplete state
     * @return True if pausing the script succeeded or was already paused
     */
    virtual bool pause();

    /**
     * Call a script function or method
     * @param name Name of the function to call
     * @param args Values to pass as actual function arguments
     * @param thisObj Object to pass as "this" if applicable
     * @param scopeObj Optional object to be used for scope resolution inside the call
     * @return Final status of the runtime after function call
     */
    virtual Status call(const String& name, ObjList& args,
	ExpOperation* thisObj = 0, ExpOperation* scopeObj = 0);

    /**
     * Check if a script has a certain function or method
     * @param name Name of the function to check
     * @return True if function exists in code
     */
    virtual bool callable(const String& name);

    /**
     * Insert an asynchronous operation to be executed
     * @param oper Operation to be inserted, will be owned by the runtime instance
     * @return True if the operation was added
     */
    virtual bool insertAsync(ScriptAsync* oper);

    /**
     * Append an asynchronous operation to be executed
     * @param oper Operation to be appended, will be owned by the runtime instance
     * @return True if the operation was added
     */
    virtual bool appendAsync(ScriptAsync* oper);

    /**
     * Try to assign a value to a single field in the script context
     * @param oper Field to assign to, contains the field name and new value
     * @param context Pointer to arbitrary object to be passed to called methods
     * @return True if assignment succeeded
     */
    bool runAssign(const ExpOperation& oper, GenObject* context = 0);

protected:
    /**
     * Resume script from where it was left, may stop and return Incomplete state
     * @return Status of the runtime after code execution
     */
    virtual Status resume();

private:
    ScriptCode* m_code;
    ScriptContext* m_context;
    Status m_state;
    ObjList m_stack;
    ObjList m_async;
};

/**
 * Abstract parser, base class for each language parser
 * @short Abstract script parser
 */
class YSCRIPT_API ScriptParser : public GenObject
{
    YCLASS(ScriptParser,GenObject)
public:
    /**
     * Destructor, releases code
     */
    virtual ~ScriptParser();

    /**
     * Parse a string as script source code
     * @param text Source code text
     * @param fragment True if the code is just an included fragment
     * @return True if the text was successfully parsed
     */
    virtual bool parse(const char* text, bool fragment = false) = 0;

    /**
     * Parse a file as script source code
     * @param name Source file name
     * @param fragment True if the code is just an included fragment
     * @return True if the file was successfully parsed
     */
    virtual bool parseFile(const char* name, bool fragment = false);

    /**
     * Clear any existing parsed code
     */
    inline void clear()
	{ setCode(0); }

    /**
     * Retrieve the currently stored parsed code
     * @return Parsed code block, may be NULL
     */
    inline ScriptCode* code() const
	{ return m_code; }

    /**
     * Create a context adequate for the parsed code
     * @return A new script context
     */
    virtual ScriptContext* createContext() const;

    /**
     * Create a runner adequate for a block of parsed code
     * @param code Parsed code block
     * @param context Script context, an empty one will be allocated if NULL
     * @return A new script runner, NULL if code is NULL
     */
    virtual ScriptRun* createRunner(ScriptCode* code, ScriptContext* context = 0) const;

    /**
     * Create a runner adequate for the parsed code
     * @param context Script context, an empty one will be allocated if NULL
     * @return A new script runner, NULL if code is not yet parsed
     */
    inline ScriptRun* createRunner(ScriptContext* context = 0) const
	{ return createRunner(code(),context); }

    /**
     * Check if a script has a certain function or method
     * @param name Name of the function to check
     * @return True if function exists in code
     */
    virtual bool callable(const String& name);

protected:
    /**
     * Default constructor for derived classes
     */
    inline ScriptParser()
	: m_code(0)
	{ }

    /**
     * Set the just parsed block of code
     * @param code Parsed code block, may be NULL
     */
    void setCode(ScriptCode* code);

private:
    ScriptCode* m_code;
};

class JsFunction;

/**
 * Javascript Object class, base for all JS objects
 * @short Javascript Object
 */
class YSCRIPT_API JsObject : public ScriptContext
{
    friend class JsFunction;
    YCLASS(JsObject,ScriptContext)
public:
    /**
     * Constructor
     * @param name Name of the object
     * @param mtx Pointer to the mutex that serializes this object
     * @param frozen True if the object is to be frozen from creation
     */
    JsObject(const char* name = "Object", Mutex* mtx = 0, bool frozen = false);

    /**
     * Destructor
     */
    virtual ~JsObject();

    /**
     * Retrieve the Mutex object used to serialize object access
     * @return Pointer to the mutex of the context this object belongs to
     */
    virtual Mutex* mutex()
	{ return m_mutex; }

    /**
     * Clone and rename method
     * @param name Name of the cloned object
     * @return New object instance
     */
    virtual JsObject* clone(const char* name) const
	{ return new JsObject(m_mutex,name); }

    /**
     * Clone method
     * @return New object instance
     */
    inline JsObject* clone() const
	{ return clone(toString()); }

    /**
     * Deep copy method
     * @param mtx Pointer to the mutex that serializes the copied object
     * @return New object instance, does not keep references to old object
     */
    virtual JsObject* copy(Mutex* mtx) const;

    /**
     * Fill a list with the unique names of all fields
     * @param names List to which key names must be added
     */
    virtual void fillFieldNames(ObjList& names);

    /**
     * Check if a certain field is assigned in the object or its prototype
     * @param stack Evaluation stack in use
     * @param name Name of the field to test
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return True if the field is present
     */
    virtual bool hasField(ObjList& stack, const String& name, GenObject* context) const;

    /**
     * Get a pointer to a field in the object or its prototype
     * @param stack Evaluation stack in use
     * @param name Name of the field to retrieve
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return Pointer to field, NULL if not present
     */
    virtual NamedString* getField(ObjList& stack, const String& name, GenObject* context) const;

    /**
     * Native constructor initialization, called by addConstructor on the prototype
     * @param construct Function that has this object as prototype
     */
    virtual void initConstructor(JsFunction* construct)
	{ }

    /**
     * Native object constructor, it's run on the prototype
     * @param stack Evaluation stack in use
     * @param oper Constructor function to evaluate
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return New created and populated Javascript object
     */
    virtual JsObject* runConstructor(ObjList& stack, const ExpOperation& oper, GenObject* context);

    /**
     * Try to evaluate a single method
     * @param stack Evaluation stack in use, parameters are popped off this stack
     *  and results are pushed back on stack
     * @param oper Function to evaluate
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return True if evaluation succeeded
     */
    virtual bool runFunction(ObjList& stack, const ExpOperation& oper, GenObject* context);

    /**
     * Try to evaluate a single field
     * @param stack Evaluation stack in use, field value must be pushed on it
     * @param oper Field to evaluate
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return True if evaluation succeeded
     */
    virtual bool runField(ObjList& stack, const ExpOperation& oper, GenObject* context);

    /**
     * Try to assign a value to a single field if object is not frozen
     * @param stack Evaluation stack in use
     * @param oper Field to assign to, contains the field name and new value
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return True if assignment succeeded
     */
    virtual bool runAssign(ObjList& stack, const ExpOperation& oper, GenObject* context);

    /**
     * Pops and evaluate the value of an operand off an evaluation stack, does not pop a barrier
     * @param stack Evaluation stack to remove the operand from
     * @param context Pointer to arbitrary object to be passed to called methods
     * @return Value removed from stack, NULL if stack underflow or field not evaluable
     */
    virtual ExpOperation* popValue(ObjList& stack, GenObject* context = 0);

    /**
     * Retrieve the object frozen status (cannot modify attributes or methods)
     * @return True if the object is frozen
     */
    inline bool frozen() const
	{ return m_frozen; }

    /**
     * Freeze the Javascript object preventing external changes to it
     */
    inline void freeze()
	{ m_frozen = true; }

    /**
     * Helper static method that adds an object to a parent
     * @param params List of parameters where to add the object
     * @param name Name of the new parameter
     * @param obj Pointer to the object to add
     */
    static void addObject(NamedList& params, const char* name, JsObject* obj);

    /**
     * Helper static method that adds a constructor to a parent
     * @param params List of parameters where to add the constructor
     * @param name Name of the new parameter
     * @param obj Pointer to the prototype object to add
     */
    static void addConstructor(NamedList& params, const char* name, JsObject* obj);

    /**
     * Helper static method that pops arguments off a stack to a list in proper order
     * @param obj Pointer to the object to use when popping each argument
     * @param stack Evaluation stack in use, parameters are popped off this stack
     * @param oper Function that is being evaluated
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @param arguments List where the arguments are added in proper order
     * @return Number of arguments popped off stack
     */
    static int extractArgs(JsObject* obj, ObjList& stack, const ExpOperation& oper,
	GenObject* context, ObjList& arguments);

    /**
     * Helper method that pops arguments off a stack to a list in proper order
     * @param stack Evaluation stack in use, parameters are popped off this stack
     * @param oper Function that is being evaluated
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @param arguments List where the arguments are added in proper order
     * @return Number of arguments popped off stack
     */
    inline int extractArgs(ObjList& stack, const ExpOperation& oper, GenObject* context, ObjList& arguments)
	{ return extractArgs(this,stack,oper,context,arguments); }

    /**
     * Create an empty function call context
     * @param mtx Pointer to the mutex that serializes this object
     * @param thisObj Optional object that will be set as "this"
     * @return New empty object usable as call context
     */
    static JsObject* buildCallContext(Mutex* mtx, JsObject* thisObj = 0);

    /**
     * Initialize the standard global objects in a context
     * @param context Script context to initialize
     */
    static void initialize(ScriptContext* context);

    /**
     * Get the name of the internal property used to track prototypes
     * @return The "__proto__" constant string
     */
    inline static const String& protoName()
	{ return s_protoName; }

    /**
     * Static helper method that deep copies all parameters
     * @param dst Destination parameters
     * @param src Source parameters
     * @param mtx Mutex to be used to synchronize all new objects
     */
    static void deepCopyParams(NamedList& dst, const NamedList& src, Mutex* mtx);

    /**
     * Helper method to return the hierarchical structure of an object
     * @param obj Object to dump structure
     * @param buf String to which the structure is added
     */
    static void dumpRecursive(const GenObject* obj, String& buf);

    /**
     * Helper method to display the hierarchical structure of an object
     * @param obj Object to display
     */
    static void printRecursive(const GenObject* obj);

protected:
    /**
     * Constructor for an empty object
     * @param mtx Pointer to the mutex that serializes this object
     * @param name Full name of the object
     * @param frozen True if the object is to be frozen from creation
     */
    JsObject(Mutex* mtx, const char* name, bool frozen = false);

    /**
     * Try to evaluate a single native method
     * @param stack Evaluation stack in use, parameters are popped off this stack
     *  and results are pushed back on stack
     * @param oper Function to evaluate
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return True if evaluation succeeded
     */
    virtual bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);

    /**
     * Retrieve the Mutex object used to serialize object access
     * @return Pointer to the mutex of the context this object belongs to
     */
    inline Mutex* mutex() const
	{ return m_mutex; }

private:
    static const String s_protoName;
    bool m_frozen;
    Mutex* m_mutex;
};

/**
 * Javascript Function class, implements user defined functions
 * @short Javascript Function
 */
class YSCRIPT_API JsFunction : public JsObject
{
    YCLASS(JsFunction,JsObject)
public:
    /**
     * Constructor
     * @param mtx Pointer to the mutex that serializes this object
     */
    JsFunction(Mutex* mtx = 0);

    /**
     * Constructor with function name
     * @param mtx Pointer to the mutex that serializes this object
     * @param name Name of the function
     * @param args Optional list of formal parameter names, will be emptied
     * @param lbl Number of the entry point label
     * @param code The script code to be used while running the function
     */
    JsFunction(Mutex* mtx, const char* name, ObjList* args = 0, long int lbl = 0,
	ScriptCode* code = 0);

    /**
     * Try to evaluate a single user defined method
     * @param stack Evaluation stack in use, parameters are popped off this stack
     *  and results are pushed back on stack
     * @param oper Function to evaluate
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @param thisObj Object that should act as "this" for the function call
     * @return True if evaluation succeeded
     */
    virtual bool runDefined(ObjList& stack, const ExpOperation& oper, GenObject* context, JsObject* thisObj = 0);

    /**
     * Retrieve the ExpFunction matching this Javascript function
     * @return Pointer to ExpFunction representation
     */
    inline const ExpFunction* getFunc() const
	{ return &m_func; }

    /**
     * Retrieve the name of the N-th formal argument
     * @param index Index of the formal argument
     * @return Pointer to formal argument name, NULL if index too large
     */
    inline const String* formalName(unsigned int index) const
	{ return static_cast<const String*>(m_formal[index]); }

    /**
     * Retrieve the entry label of the code for this function
     * @return Number of the entry point label, zero if no code defined
     */
    inline long int label() const
	{ return m_label; }

    /**
     * Deep copy method
     * @param mtx Pointer to the mutex that serializes the copied array
     * @return New object instance, does not keep references to old array
     */
    virtual JsObject* copy(Mutex* mtx) const;

protected:
    /**
     * Try to evaluate a single native method
     * @param stack Evaluation stack in use, parameters are popped off this stack
     *  and results are pushed back on stack
     * @param oper Function to evaluate
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return True if evaluation succeeded
     */
    virtual bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);

private:
    void init();
    ObjList m_formal;
    long int m_label;
    ScriptCode* m_code;
    ExpFunction m_func;
};

/**
 * Javascript Array class, implements arrays of items
 * @short Javascript Array
 */
class YSCRIPT_API JsArray : public JsObject
{
    YCLASS(JsArray,JsObject)
public:
    /**
     * Constructor
     * @param mtx Pointer to the mutex that serializes this object
     */
    JsArray(Mutex* mtx = 0);

    /**
     * Retrieve the length of the array
     * @return Number of numerically indexed objects in array
     */
    inline long length() const
	{ return m_length; }

    /**
     * Add an item at the end of the array
     * @param item Item to add to array
     */
    void push(ExpOperation* item);

    /**
     * Deep copy method
     * @param mtx Pointer to the mutex that serializes the copied array
     * @return New object instance, does not keep references to old array
     */
    virtual JsObject* copy(Mutex* mtx) const;

protected:
    /*
     * Constructor for an empty array
     * @param mtx Pointer to the mutex that serializes this object
     * @param name Full name of the object
     * @param frozen True if the object is to be frozen from creation
     */
    inline JsArray(Mutex* mtx, const char* name, bool frozen = false)
	: JsObject(mtx,name,frozen), m_length(0)
	{ }

    /**
     * Clone and rename method
     * @param name Name of the cloned object
     * @return New object instance
     */
    virtual JsObject* clone(const char* name) const
	{ return new JsArray(mutex(),name); }

    /**
     * Try to evaluate a single native method
     * @param stack Evaluation stack in use, parameters are popped off this stack
     *  and results are pushed back on stack
     * @param oper Function to evaluate
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return True if evaluation succeeded
     */
    bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);

    /**
     * Synchronize the "length" parameter to the internally stored length
     */
    inline void setLength()
	{ params().setParam("length",String((int)m_length)); }

    /**
     * Set the internal length and the "length" parameter to a specific value
     * @param len Length of array to set
     */
    inline void setLength(long len)
	{ m_length = len; params().setParam("length",String((int)len)); }

private:
    bool runNativeSlice(ObjList& stack, const ExpOperation& oper, GenObject* context);
    bool runNativeSplice(ObjList& stack, const ExpOperation& oper, GenObject* context);
    bool runNativeSort(ObjList& stack, const ExpOperation& oper, GenObject* context);
    long m_length;
};

/**
 * Javascript RegExp class, implements regular expression matching
 * @short Javascript RegExp
 */
class YSCRIPT_API JsRegExp : public JsObject
{
    YCLASS(JsRegExp,JsObject)
public:
    /**
     * Constructor for a RegExp constructor
     * @param mtx Pointer to the mutex that serializes this object
     */
    JsRegExp(Mutex* mtx = 0);

    /**
     * Constructor for a RegExp object
     * @param mtx Pointer to the mutex that serializes this object
     * @param name Full name of the object
     * @param rexp Regular expression text
     * @param insensitive True to not differentiate case
     * @param extended True to use POSIX Extended Regular Expression syntax
     * @param frozen True to create an initially frozen object
     */
    JsRegExp(Mutex* mtx, const char* name, const char* rexp = 0, bool insensitive = false,
	bool extended = true, bool frozen = false);

    /**
     * Access the internal Regexp object that does the matching
     * @return Const reference to the internal Regexp object
     */
    inline const Regexp& regexp() const
	{ return m_regexp; }

    /**
     * Access the internal Regexp object that does the matching
     * @return Reference to the internal Regexp object
     */
    inline Regexp& regexp()
	{ return m_regexp; }

protected:
    /**
     * Clone and rename method
     * @param name Name of the cloned object
     * @return New object instance
     */
    virtual JsObject* clone(const char* name) const
	{ return new JsRegExp(mutex(),name,m_regexp.c_str(),
	    m_regexp.isCaseInsensitive(),m_regexp.isExtended()); }

    /**
     * Try to evaluate a single native method
     * @param stack Evaluation stack in use, parameters are popped off this stack
     *  and results are pushed back on stack
     * @param oper Function to evaluate
     * @param context Pointer to arbitrary object passed from evaluation methods
     * @return True if evaluation succeeded
     */
    bool runNative(ObjList& stack, const ExpOperation& oper, GenObject* context);

private:
    Regexp m_regexp;
};

/**
 * Javascript parser, takes source code and generates preparsed code
 * @short Javascript parser
 */
class YSCRIPT_API JsParser : public ScriptParser
{
    YCLASS(JsParser,ScriptParser)
public:
    /**
     * Constructor
     * @param allowLink True to allow linking of the code, false otherwise.
     */
    inline JsParser(bool allowLink = true)
	: m_allowLink(allowLink)
	{ }

    /**
     * Parse a string as Javascript source code
     * @param text Source code text
     * @param fragment True if the code is just an included fragment
     * @return True if the text was successfully parsed
     */
    virtual bool parse(const char* text, bool fragment = false);

    /**
     * Create a context adequate for Javascript code
     * @return A new Javascript context
     */
    virtual ScriptContext* createContext() const;

    /**
     * Create a runner adequate for a block of parsed Javascript code
     * @param code Parsed code block
     * @param context Javascript context, an empty one will be allocated if NULL
     * @return A new Javascript runner, NULL if code is NULL
     */
    virtual ScriptRun* createRunner(ScriptCode* code, ScriptContext* context = 0) const;

    /**
     * Create a runner adequate for the parsed Javascript code
     * @param context Javascript context, an empty one will be allocated if NULL
     * @return A new Javascript runner, NULL if code is not yet parsed
     */
    inline ScriptRun* createRunner(ScriptContext* context = 0) const
	{ return createRunner(code(),context); }

    /**
     * Check if a script has a certain function or method
     * @param name Name of the function to check
     * @return True if function exists in code
     */
    virtual bool callable(const String& name);

    /**
     * Adjust a file script path to include default if needed
     * @param script File path to adjust
     */
    void adjustPath(String& script) const;

    /**
     * Retrieve the base script path
     * @return Base path added to relative script paths
     */
    inline const String& basePath() const
	{ return m_basePath; }

    /**
     * Set the pase script path
     * @param path Base path to add to relative script paths
     */
    inline void basePath(const char* path)
	{ m_basePath = path; }

    /**
     * Set whether the Javascript code should be linked or not
     * @param allowed True to allow linking, false otherwise
     */
    inline void link(bool allowed = true)
	{ m_allowLink = allowed; }

    /**
     * Parse and run a piece of Javascript code
     * @param text Source code fragment to execute
     * @param result Pointer to an optional pointer to store returned value
     * @param context Script context, an empty one will be allocated if NULL
     * @return Status of the runtime after code execution
     */
    static ScriptRun::Status eval(const String& text, ExpOperation** result = 0, ScriptContext* context = 0);

    /**
     * Parse a complete block of JSON text
     * @param text JSON text to parse
     * @return JsObject holding the content of JSON, must be dereferenced after use, NULL if parse error
     */
    static JsObject* parseJSON(const char* text);

    /**
     * Get a "null" object wrapper that will identity match another "null"
     * @param name Name of the new wrapper, "null" if empty
     * @return ExpWrapper for the "null" object
     */
    static ExpOperation* nullClone(const char* name = 0);

    /**
     * Check if an operation holds a null value
     * @return True if the operation holds a null object
     */
    static bool isNull(const ExpOperation& oper);

    /**
     * Check if an operation holds an undefined value
     * @return True if the operation holds an undefined value
     */
    static bool isUndefined(const ExpOperation& oper);

private:
    String m_basePath;
    bool m_allowLink;
};

}; // namespace TelEngine

#endif /* __YATESCRIPT_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
