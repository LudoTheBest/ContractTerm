#ifndef FH_COMP_CONTRACT_TERM_H
#define FH_COMP_CONTRACT_TERM_H

#include <boost/date_time/gregorian/gregorian.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace FH {
namespace COMP {

enum class ErrorCode {
    invalid_configuration,
    missing_required_field,
    invalid_character,
    disallowed_value,
    unknown_month,
    unknown_option_type,
    invalid_numeric_value,
    numeric_overflow,
    unsupported_precision,
    invalid_date,
    unsupported_date,
    missing_maturity_day,
    year_resolution_failed,
    ambiguous_year,
    ambiguous_symbol,
    unconsumed_input,
    unsupported_cycle
};

struct Error {
    ErrorCode code;
    std::string message;
    std::string field;
    std::size_t position;
    bool hasPosition;

    Error();
    Error(ErrorCode errorCode, const std::string& diagnostic,
          const std::string& fieldName = std::string());
    Error(ErrorCode errorCode, const std::string& diagnostic,
          const std::string& fieldName, std::size_t inputPosition);
};

template <typename T>
class Result {
public:
    static Result success(const T& value) {
        return Result(value);
    }

    static Result failure(const Error& error) {
        return Result(error);
    }

    bool hasValue() const { return hasValue_; }
    explicit operator bool() const { return hasValue_; }

    const T& value() const {
        assert(hasValue_);
        return value_;
    }
    const Error& error() const {
        assert(!hasValue_);
        return error_;
    }

private:
    explicit Result(const T& value)
        : hasValue_(true), value_(value), error_() {}

    explicit Result(const Error& error)
        : hasValue_(false), value_(), error_(error) {}

    bool hasValue_;
    T value_;
    Error error_;
};

enum class InstrumentKind {
    future,
    option
};

enum class ExpirationCycle {
    yearly,
    halfYearly,
    quarterly,
    biMonthly,
    monthly,
    weekly,
    daily,
    rollingSpot
};

enum class FieldKind {
    literal,
    ticker,
    year,
    month,
    day,
    optionType,
    strike,
    metadata
};

enum class DecimalEncoding {
    explicitDecimal,
    impliedDecimal
};

struct Decimal {
    std::uint64_t coefficient;
    unsigned int scale;

    Decimal();
    Decimal(std::uint64_t value, unsigned int decimalPlaces);
    std::string toString() const;
};

struct FieldSpec {
    FieldKind kind;
    std::string name;
    std::size_t position;
    bool hasPosition;
    std::size_t minLength;
    std::size_t maxLength;
    std::string literal;
    std::string allowedCharacters;
    std::vector<std::string> allowedValues;
    std::map<std::string, int> mapping;
    unsigned int yearDigits;
    DecimalEncoding decimalEncoding;
    unsigned int impliedDecimalPlaces;

    FieldSpec();

    static FieldSpec literalField(const std::string& value);
    static FieldSpec field(FieldKind fieldKind, const std::string& fieldName,
                           std::size_t minimumLength, std::size_t maximumLength);
};

struct FormatConfig {
    std::vector<FieldSpec> fields;
    bool requireYearAndMonth;

    FormatConfig();

    static FormatConfig defaultFutures();
    static FormatConfig defaultOptions();
};

struct YearResolution {
    int referenceYear;
    int minimumOffset;
    int maximumOffset;

    YearResolution();
    YearResolution(int reference, int minimumInclusiveOffset,
                   int maximumInclusiveOffset);
};

struct ParsedContract {
    InstrumentKind instrument;
    bool hasTicker;
    std::string ticker;
    bool hasYear;
    int year;
    bool hasMonth;
    int month;
    bool hasDay;
    int day;
    bool hasOptionType;
    char optionType;
    bool hasStrike;
    Decimal strike;
    std::map<std::string, std::string> metadata;

    ParsedContract();
};

class ContractTerm {
public:
    const std::string& value() const;

private:
    explicit ContractTerm(const std::string& value);
    std::string value_;

    friend Result<ContractTerm> buildContractTerm(
        const ParsedContract&, ExpirationCycle);
};

Result<ExpirationCycle> expirationCycleFromCode(const std::string& code);
Result<ParsedContract> parseSymbol(
    const std::string& symbol, const FormatConfig& format,
    InstrumentKind instrument, const YearResolution& yearResolution);
Result<ContractTerm> buildContractTerm(
    const ParsedContract& parsed, ExpirationCycle cycle);

} // namespace COMP
} // namespace FH

#endif
