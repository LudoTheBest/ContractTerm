#include "ContractTerm.h"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

namespace FH {
namespace COMP {
namespace {

const std::size_t kNoPosition = std::numeric_limits<std::size_t>::max();
const std::size_t kMaximumFieldLength = 256;
const std::size_t kMaximumSymbolLength = 4096;
const int kMinimumSupportedYear = 1400;
const int kMaximumSupportedYear = 9999;
const std::uint64_t kMaximumDecimalCoefficient =
    (std::numeric_limits<std::uint64_t>::max)();
const std::size_t kMaximumFormatFields = 128;
const std::size_t kMaximumParseBranches = 1000000;

int errorPriority(ErrorCode code) {
    switch (code) {
    case ErrorCode::invalid_configuration:
        return 100;
    case ErrorCode::ambiguous_year:
    case ErrorCode::ambiguous_symbol:
    case ErrorCode::numeric_overflow:
    case ErrorCode::unsupported_precision:
        return 90;
    case ErrorCode::year_resolution_failed:
    case ErrorCode::unsupported_date:
    case ErrorCode::invalid_date:
    case ErrorCode::unknown_month:
    case ErrorCode::unknown_option_type:
        return 80;
    case ErrorCode::disallowed_value:
    case ErrorCode::invalid_numeric_value:
    case ErrorCode::invalid_character:
        return 70;
    case ErrorCode::missing_required_field:
    case ErrorCode::missing_maturity_day:
        return 60;
    case ErrorCode::unconsumed_input:
        return 50;
    case ErrorCode::unsupported_cycle:
        return 40;
    }
    return 0;
}

Error makeError(ErrorCode code, const std::string& message,
                const std::string& field, std::size_t position) {
    return Error(code, message, field, position);
}

bool isDigitString(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    for (std::string::const_iterator it = value.begin(); it != value.end(); ++it) {
        if (*it < '0' || *it > '9') {
            return false;
        }
    }
    return true;
}

bool containsCharacter(const std::string& allowed, char value) {
    return allowed.find(value) != std::string::npos;
}

bool parseUnsigned(const std::string& text, std::uint64_t maximum,
                   std::uint64_t& result) {
    if (!isDigitString(text)) {
        return false;
    }
    std::uint64_t value = 0;
    for (std::string::const_iterator it = text.begin(); it != text.end(); ++it) {
        const unsigned int digit = static_cast<unsigned int>(*it - '0');
        if (value > (maximum - digit) / 10U) {
            return false;
        }
        value = value * 10U + digit;
    }
    result = value;
    return true;
}

bool sameParsedContract(const ParsedContract& left, const ParsedContract& right) {
    return left.instrument == right.instrument &&
           left.hasTicker == right.hasTicker && left.ticker == right.ticker &&
           left.hasYear == right.hasYear && left.year == right.year &&
           left.hasMonth == right.hasMonth && left.month == right.month &&
           left.hasDay == right.hasDay && left.day == right.day &&
           left.hasOptionType == right.hasOptionType &&
           left.optionType == right.optionType &&
           left.hasStrike == right.hasStrike &&
           left.strike.coefficient == right.strike.coefficient &&
           left.strike.scale == right.strike.scale &&
           left.metadata == right.metadata;
}

Error resolveYear(const std::string& text, const FieldSpec& field,
                  const YearResolution& resolution, int& resolved,
                  std::size_t position) {
    if (!isDigitString(text)) {
        return makeError(ErrorCode::invalid_numeric_value,
                         "Year must contain decimal digits only.",
                         field.name, position);
    }

    const unsigned int digits =
        field.yearDigits == 0 ? static_cast<unsigned int>(text.size())
                              : field.yearDigits;
    if ((digits != 1 && digits != 2 && digits != 4) || text.size() != digits) {
        return makeError(ErrorCode::invalid_configuration,
                         "Year field length must match a one-, two-, or four-digit encoding.",
                         field.name, position);
    }

    std::uint64_t suffixValue = 0;
    if (!parseUnsigned(text, static_cast<std::uint64_t>(kMaximumSupportedYear),
                       suffixValue)) {
        return makeError(ErrorCode::invalid_numeric_value,
                         "Year field is not a supported decimal number.",
                         field.name, position);
    }

    if (digits == 4) {
        if (suffixValue < static_cast<std::uint64_t>(kMinimumSupportedYear) ||
            suffixValue > static_cast<std::uint64_t>(kMaximumSupportedYear)) {
            return makeError(ErrorCode::unsupported_date,
                             "Year is outside Boost Gregorian's supported range (1400-9999).",
                             field.name, position);
        }
        resolved = static_cast<int>(suffixValue);
        return Error();
    }

    if (resolution.minimumOffset > resolution.maximumOffset) {
        return makeError(ErrorCode::invalid_configuration,
                         "Year-resolution minimum offset must not exceed maximum offset.",
                         field.name, position);
    }

    const long long windowFirst =
        static_cast<long long>(resolution.referenceYear) + resolution.minimumOffset;
    const long long windowLast =
        static_cast<long long>(resolution.referenceYear) + resolution.maximumOffset;
    const long long firstYear = std::max(
        windowFirst, static_cast<long long>(kMinimumSupportedYear));
    const long long lastYear = std::min(
        windowLast, static_cast<long long>(kMaximumSupportedYear));
    const std::uint64_t modulus = digits == 1 ? 10U : 100U;
    if (firstYear > lastYear) {
        return makeError(ErrorCode::year_resolution_failed,
                         "Year suffix does not match a supported year in the configured inclusive window.",
                         field.name, position);
    }
    const std::uint64_t firstRemainder =
        static_cast<std::uint64_t>(firstYear) % modulus;
    const std::uint64_t offset =
        (suffixValue + modulus - firstRemainder) % modulus;
    const long long firstMatch = firstYear + static_cast<long long>(offset);
    if (firstMatch > lastYear) {
        return makeError(ErrorCode::year_resolution_failed,
                         "Year suffix does not match a supported year in the configured inclusive window.",
                         field.name, position);
    }
    if (firstMatch + static_cast<long long>(modulus) <= lastYear) {
        return makeError(ErrorCode::ambiguous_year,
                         "Year suffix matches multiple years in the configured inclusive window.",
                         field.name, position);
    }
    resolved = static_cast<int>(firstMatch);
    return Error();
}

Error parseStrike(const std::string& text, const FieldSpec& field,
                  Decimal& decimal, std::size_t position) {
    if (field.impliedDecimalPlaces > 18) {
        return makeError(ErrorCode::unsupported_precision,
                         "Strike precision cannot exceed 18 decimal places.",
                         field.name, position);
    }

    std::string digits;
    unsigned int scale = 0;
    if (field.decimalEncoding == DecimalEncoding::impliedDecimal) {
        if (!isDigitString(text)) {
            return makeError(ErrorCode::invalid_numeric_value,
                             "Implied-decimal strike must contain digits only.",
                             field.name, position);
        }
        digits = text;
        scale = field.impliedDecimalPlaces;
    } else {
        bool sawPoint = false;
        bool sawDigit = false;
        bool sawFractionDigit = false;
        for (std::string::const_iterator it = text.begin(); it != text.end(); ++it) {
            if (*it == '.') {
                if (sawPoint) {
                    return makeError(ErrorCode::invalid_numeric_value,
                                     "Strike may contain only one decimal point.",
                                     field.name, position);
                }
                sawPoint = true;
            } else if (*it >= '0' && *it <= '9') {
                sawDigit = true;
                digits.push_back(*it);
                if (sawPoint) {
                    sawFractionDigit = true;
                    ++scale;
                }
            } else {
                return makeError(ErrorCode::invalid_character,
                                 "Strike may contain decimal digits and an optional decimal point only.",
                                 field.name, position);
            }
        }
        if (!sawDigit || (sawPoint && !sawFractionDigit)) {
            return makeError(ErrorCode::invalid_numeric_value,
                             "Strike must contain digits and, when present, digits after its decimal point.",
                             field.name, position);
        }
    }

    if (scale > 18) {
        return makeError(ErrorCode::unsupported_precision,
                         "Strike precision cannot exceed 18 decimal places.",
                         field.name, position);
    }

    std::uint64_t coefficient = 0;
    if (!parseUnsigned(digits, kMaximumDecimalCoefficient, coefficient)) {
        return makeError(ErrorCode::numeric_overflow,
                         "Strike coefficient exceeds the supported unsigned 64-bit range.",
                         field.name, position);
    }
    decimal = Decimal(coefficient, scale);
    return Error();
}

Error validateFormat(const FormatConfig& format, InstrumentKind instrument) {
    if (instrument != InstrumentKind::future &&
        instrument != InstrumentKind::option) {
        return Error(ErrorCode::invalid_configuration,
                     "Instrument kind must be explicitly future or option.");
    }
    if (format.fields.empty()) {
        return Error(ErrorCode::invalid_configuration,
                     "A symbol format must contain at least one field.");
    }
    if (format.fields.size() > kMaximumFormatFields) {
        return Error(ErrorCode::invalid_configuration,
                     "A symbol format may contain at most 128 fields.");
    }

    std::set<FieldKind> semanticFields;
    std::set<std::string> metadataNames;
    bool hasTicker = false;
    bool hasOptionType = false;
    bool hasStrike = false;
    std::size_t cursorMinimum = 0;
    std::size_t cursorMaximum = 0;
    std::size_t branchCount = 1;

    for (std::size_t i = 0; i < format.fields.size(); ++i) {
        const FieldSpec& field = format.fields[i];
        if (field.kind != FieldKind::literal &&
            field.kind != FieldKind::ticker && field.kind != FieldKind::year &&
            field.kind != FieldKind::month && field.kind != FieldKind::day &&
            field.kind != FieldKind::optionType &&
            field.kind != FieldKind::strike &&
            field.kind != FieldKind::metadata) {
            return Error(ErrorCode::invalid_configuration,
                         "Field kind is not supported.");
        }
        if (field.kind != FieldKind::literal && field.name.empty()) {
            return Error(ErrorCode::invalid_configuration,
                         "Non-literal fields require a nonempty name.");
        }
        if (field.kind != FieldKind::literal && field.kind != FieldKind::metadata &&
            !semanticFields.insert(field.kind).second) {
            return Error(ErrorCode::invalid_configuration,
                         "A semantic field may appear only once in a symbol format.",
                         field.name);
        }
        if (field.kind == FieldKind::ticker) {
            hasTicker = true;
        } else if (field.kind == FieldKind::optionType) {
            hasOptionType = true;
        } else if (field.kind == FieldKind::strike) {
            hasStrike = true;
        } else if (field.kind == FieldKind::metadata) {
            if (field.name.empty() || !metadataNames.insert(field.name).second) {
                return Error(ErrorCode::invalid_configuration,
                             "Metadata fields require unique, nonempty names.",
                             field.name);
            }
        }

        if (field.minLength == 0 || field.maxLength < field.minLength ||
            field.maxLength > kMaximumFieldLength) {
            return Error(ErrorCode::invalid_configuration,
                         "Field length bounds must satisfy 1 <= minimum <= maximum <= 256.",
                         field.name);
        }
        const std::size_t fieldBranches =
            field.maxLength - field.minLength + 1;
        if (branchCount > kMaximumParseBranches / fieldBranches) {
            return Error(ErrorCode::invalid_configuration,
                         "Variable field bounds permit more than one million parse splits; narrow the bounds.",
                         field.name);
        }
        branchCount *= fieldBranches;
        if (field.kind == FieldKind::literal &&
            (field.literal.empty() || field.minLength != field.literal.size() ||
             field.maxLength != field.literal.size())) {
            return Error(ErrorCode::invalid_configuration,
                         "Literal fields must have a nonempty value and an exact matching length.",
                         field.name);
        }
        if (field.kind == FieldKind::year &&
            field.yearDigits != 0 && field.yearDigits != 1 &&
            field.yearDigits != 2 && field.yearDigits != 4) {
            return Error(ErrorCode::invalid_configuration,
                         "Year encoding must be one, two, or four digits, or zero for length-driven encoding.",
                         field.name);
        }
        if (field.kind == FieldKind::year) {
            if (field.yearDigits != 0 &&
                (field.minLength != field.yearDigits ||
                 field.maxLength != field.yearDigits)) {
                return Error(ErrorCode::invalid_configuration,
                             "A fixed year encoding must use a field length equal to its digit count.",
                             field.name);
            }
            if (field.yearDigits == 0) {
                for (std::size_t length = field.minLength;
                     length <= field.maxLength; ++length) {
                    if (length != 1 && length != 2 && length != 4) {
                        return Error(ErrorCode::invalid_configuration,
                                     "Length-driven year fields may include only one-, two-, or four-digit lengths.",
                                     field.name);
                    }
                }
            }
        }
        if (field.kind == FieldKind::strike && field.impliedDecimalPlaces > 18) {
            return Error(ErrorCode::unsupported_precision,
                         "Strike precision cannot exceed 18 decimal places.",
                         field.name);
        }
        if (!field.mapping.empty() && field.kind != FieldKind::month &&
            field.kind != FieldKind::optionType) {
            return Error(ErrorCode::invalid_configuration,
                         "Mappings are supported only for month and option-type fields.",
                         field.name);
        }
        if (field.kind == FieldKind::strike &&
            field.decimalEncoding != DecimalEncoding::explicitDecimal &&
            field.decimalEncoding != DecimalEncoding::impliedDecimal) {
            return Error(ErrorCode::invalid_configuration,
                         "Strike decimal encoding is not supported.", field.name);
        }
        if (field.hasPosition) {
            if (field.position > kMaximumSymbolLength ||
                field.maxLength > kMaximumSymbolLength - field.position) {
                return Error(ErrorCode::invalid_configuration,
                             "Explicit field position is outside the supported symbol range.",
                             field.name);
            }
            if (field.position < cursorMinimum ||
                field.position > cursorMaximum) {
                return Error(ErrorCode::invalid_configuration,
                             "Explicit position is unreachable because it overlaps a field or leaves an unparsed gap.",
                             field.name);
            }
            cursorMinimum = field.position + field.minLength;
            cursorMaximum = field.position + field.maxLength;
        } else {
            cursorMinimum += field.minLength;
            cursorMaximum += field.maxLength;
        }
        if (cursorMinimum > kMaximumSymbolLength) {
            return Error(ErrorCode::invalid_configuration,
                         "Minimum field lengths exceed the supported symbol size.",
                         field.name);
        }

        for (std::map<std::string, int>::const_iterator mapping =
                 field.mapping.begin();
             mapping != field.mapping.end(); ++mapping) {
            if (mapping->first.empty()) {
                return Error(ErrorCode::invalid_configuration,
                             "Mapping keys must not be empty.", field.name);
            }
            if (field.kind == FieldKind::month &&
                (mapping->second < 1 || mapping->second > 12)) {
                return Error(ErrorCode::invalid_configuration,
                             "Month mapping values must be between 1 and 12.",
                             field.name);
            }
            if (field.kind == FieldKind::optionType &&
                mapping->second != 1 && mapping->second != 2) {
                return Error(ErrorCode::invalid_configuration,
                             "Option type mappings must use 1 for call or 2 for put.",
                             field.name);
            }
        }
    }

    if (!hasTicker) {
        return Error(ErrorCode::invalid_configuration,
                     "Every symbol format must contain a ticker field.",
                     "ticker");
    }
    if (instrument == InstrumentKind::option && (!hasOptionType || !hasStrike)) {
        return Error(ErrorCode::invalid_configuration,
                     "Option formats must contain both option-type and strike fields.");
    }
    if (instrument == InstrumentKind::future && (hasOptionType || hasStrike)) {
        return Error(ErrorCode::invalid_configuration,
                     "Future formats must not contain option-type or strike fields.");
    }
    return Error();
}

Error readField(const std::string& text, const FieldSpec& field,
                const YearResolution& yearResolution, ParsedContract& parsed,
                std::size_t position) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (!field.allowedCharacters.empty() &&
            !containsCharacter(field.allowedCharacters, text[i])) {
            return makeError(ErrorCode::invalid_character,
                             "Field contains a character not permitted by its configuration.",
                             field.name, position + i);
        }
    }
    if (!field.allowedValues.empty() &&
        std::find(field.allowedValues.begin(), field.allowedValues.end(), text) ==
            field.allowedValues.end()) {
        ErrorCode code = field.kind == FieldKind::month
                             ? ErrorCode::unknown_month
                             : field.kind == FieldKind::optionType
                                   ? ErrorCode::unknown_option_type
                                   : ErrorCode::disallowed_value;
        return makeError(code, "Field value is not in its configured allowed values.",
                         field.name, position);
    }

    switch (field.kind) {
    case FieldKind::literal:
        if (text != field.literal) {
            return makeError(ErrorCode::disallowed_value,
                             "Literal does not match the configured symbol format.",
                             field.name, position);
        }
        break;
    case FieldKind::ticker:
        parsed.hasTicker = true;
        parsed.ticker = text;
        break;
    case FieldKind::year: {
        int year = 0;
        Error error = resolveYear(text, field, yearResolution, year, position);
        if (!error.message.empty()) {
            return error;
        }
        parsed.hasYear = true;
        parsed.year = year;
        break;
    }
    case FieldKind::month: {
        int month = 0;
        if (!field.mapping.empty()) {
            std::map<std::string, int>::const_iterator found =
                field.mapping.find(text);
            if (found == field.mapping.end()) {
                return makeError(ErrorCode::unknown_month,
                                 "Month code is not present in the configured month mapping.",
                                 field.name, position);
            }
            month = found->second;
        } else {
            std::uint64_t number = 0;
            if (!parseUnsigned(text, 12, number) || number < 1) {
                return makeError(ErrorCode::invalid_date,
                                 "Month must be a decimal value from 1 through 12.",
                                 field.name, position);
            }
            month = static_cast<int>(number);
        }
        parsed.hasMonth = true;
        parsed.month = month;
        break;
    }
    case FieldKind::day: {
        std::uint64_t number = 0;
        if (!parseUnsigned(text, 31, number) || number < 1) {
            return makeError(ErrorCode::invalid_numeric_value,
                             "Day must be a decimal value from 1 through 31.",
                             field.name, position);
        }
        parsed.hasDay = true;
        parsed.day = static_cast<int>(number);
        break;
    }
    case FieldKind::optionType: {
        char type = '\0';
        if (!field.mapping.empty()) {
            std::map<std::string, int>::const_iterator found =
                field.mapping.find(text);
            if (found == field.mapping.end()) {
                return makeError(ErrorCode::unknown_option_type,
                                 "Option-type code is not present in the configured mapping.",
                                 field.name, position);
            }
            type = found->second == 1 ? 'C' : 'P';
        } else if (text == "C" || text == "P") {
            type = text[0];
        } else {
            return makeError(ErrorCode::unknown_option_type,
                             "Option type must map to call or put.",
                             field.name, position);
        }
        parsed.hasOptionType = true;
        parsed.optionType = type;
        break;
    }
    case FieldKind::strike: {
        Decimal strike;
        Error error = parseStrike(text, field, strike, position);
        if (!error.message.empty()) {
            return error;
        }
        parsed.hasStrike = true;
        parsed.strike = strike;
        break;
    }
    case FieldKind::metadata:
        parsed.metadata[field.name] = text;
        break;
    }
    return Error();
}

class SymbolParser {
public:
    SymbolParser(const std::string& symbol, const FormatConfig& format,
                 InstrumentKind instrument,
                 const YearResolution& yearResolution)
        : symbol_(symbol), format_(format), instrument_(instrument),
          yearResolution_(yearResolution), firstSuccess_(), hasSuccess_(false),
          ambiguous_(false), lastError_(), hasError_(false) {}

    Result<ParsedContract> run() {
        ParsedContract initial;
        initial.instrument = instrument_;
        if (symbol_.size() > kMaximumSymbolLength) {
            return Result<ParsedContract>::failure(
                Error(ErrorCode::unconsumed_input,
                      "Symbol exceeds the supported 4096-character limit."));
        }
        walk(0, 0, initial);
        if (ambiguous_) {
            return Result<ParsedContract>::failure(
                Error(ErrorCode::ambiguous_symbol,
                      "Multiple valid field splits produce different contract information."));
        }
        if (!hasSuccess_) {
            if (hasError_) {
                return Result<ParsedContract>::failure(lastError_);
            }
            return Result<ParsedContract>::failure(
                Error(ErrorCode::unconsumed_input,
                      "Symbol does not match the configured format."));
        }
        return Result<ParsedContract>::success(firstSuccess_);
    }

private:
    void remember(const Error& error) {
        if (!error.message.empty() &&
            (!hasError_ ||
             errorPriority(error.code) >= errorPriority(lastError_.code))) {
            lastError_ = error;
            hasError_ = true;
        }
    }

    void walk(std::size_t fieldIndex, std::size_t cursor,
              const ParsedContract& parsed) {
        if (ambiguous_) {
            return;
        }
        if (fieldIndex == format_.fields.size()) {
            if (cursor != symbol_.size()) {
                remember(makeError(ErrorCode::unconsumed_input,
                                   "Input remains after the configured fields.",
                                   std::string(), cursor));
                return;
            }
            if (!parsed.hasTicker || parsed.ticker.empty()) {
                remember(Error(ErrorCode::missing_required_field,
                               "A nonempty ticker is required.", "ticker"));
                return;
            }
            if (format_.requireYearAndMonth &&
                (!parsed.hasYear || !parsed.hasMonth)) {
                remember(Error(ErrorCode::missing_required_field,
                               "This default format requires maturity year and month.",
                               !parsed.hasYear ? "year" : "month"));
                return;
            }
            if (instrument_ == InstrumentKind::option &&
                (!parsed.hasOptionType || !parsed.hasStrike)) {
                remember(Error(ErrorCode::missing_required_field,
                               "Options require option type and strike fields."));
                return;
            }
            if (!hasSuccess_) {
                firstSuccess_ = parsed;
                hasSuccess_ = true;
            } else if (!sameParsedContract(firstSuccess_, parsed)) {
                ambiguous_ = true;
            }
            return;
        }

        const FieldSpec& field = format_.fields[fieldIndex];
        std::size_t start = cursor;
        if (field.hasPosition) {
            if (cursor != field.position) {
                remember(makeError(
                    cursor < field.position ? ErrorCode::unconsumed_input
                                            : ErrorCode::invalid_configuration,
                    cursor < field.position
                        ? "Explicit field position leaves unmatched input."
                        : "A variable field overlaps the next explicit position.",
                    field.name, cursor));
                return;
            }
            start = field.position;
        }
        if (start > symbol_.size()) {
            remember(makeError(ErrorCode::unconsumed_input,
                               "Field begins beyond the end of the symbol.",
                               field.name, start));
            return;
        }

        const std::size_t remaining = symbol_.size() - start;
        const std::size_t maximum = std::min(field.maxLength, remaining);
        if (field.kind == FieldKind::literal) {
            if (field.literal.size() > remaining ||
                symbol_.compare(start, field.literal.size(), field.literal) != 0) {
                remember(makeError(ErrorCode::disallowed_value,
                                   "Expected configured literal was not found.",
                                   field.name, start));
                return;
            }
            ParsedContract next = parsed;
            walk(fieldIndex + 1, start + field.literal.size(), next);
            return;
        }

        if (maximum < field.minLength) {
            remember(makeError(ErrorCode::missing_required_field,
                               "Input ended before the field's minimum length.",
                               field.name, start));
            return;
        }

        for (std::size_t length = field.minLength; length <= maximum; ++length) {
            const std::string text = symbol_.substr(start, length);
            ParsedContract next = parsed;
            Error error = readField(text, field, yearResolution_, next, start);
            if (!error.message.empty()) {
                remember(error);
                continue;
            }
            walk(fieldIndex + 1, start + length, next);
        }
    }

    const std::string& symbol_;
    const FormatConfig& format_;
    InstrumentKind instrument_;
    const YearResolution& yearResolution_;
    ParsedContract firstSuccess_;
    bool hasSuccess_;
    bool ambiguous_;
    Error lastError_;
    bool hasError_;
};

Error validateMaturity(const ParsedContract& parsed) {
    if (parsed.hasYear &&
        (parsed.year < kMinimumSupportedYear ||
         parsed.year > kMaximumSupportedYear)) {
        return Error(ErrorCode::unsupported_date,
                     "Year is outside Boost Gregorian's supported range (1400-9999).",
                     "year");
    }
    if (parsed.hasMonth && (parsed.month < 1 || parsed.month > 12)) {
        return Error(ErrorCode::invalid_date, "Month must be between 1 and 12.",
                     "month");
    }
    if (parsed.hasDay && (parsed.day < 1 || parsed.day > 31)) {
        return Error(ErrorCode::invalid_date, "Day must be between 1 and 31.",
                     "day");
    }
    if (parsed.hasDay && (!parsed.hasYear || !parsed.hasMonth)) {
        return Error(ErrorCode::invalid_date,
                     "A maturity day can be validated only when both year and month are present.",
                     "day");
    }

    if (parsed.hasYear && parsed.hasMonth) {
        try {
            if (parsed.hasDay) {
                const boost::gregorian::date date(
                    static_cast<unsigned short>(parsed.year),
                    static_cast<unsigned short>(parsed.month),
                    static_cast<unsigned short>(parsed.day));
                (void)date;
            } else {
                const boost::gregorian::date monthDate(
                    static_cast<unsigned short>(parsed.year),
                    static_cast<unsigned short>(parsed.month), 1);
                (void)monthDate;
            }
        } catch (const boost::gregorian::bad_year&) {
            return Error(ErrorCode::unsupported_date,
                         "Year is outside Boost Gregorian's supported range (1400-9999).",
                         "year");
        } catch (const boost::gregorian::bad_month&) {
            return Error(ErrorCode::invalid_date,
                         "Month is not a valid Gregorian month.", "month");
        } catch (const boost::gregorian::bad_day_of_month&) {
            return Error(ErrorCode::invalid_date,
                         "Day is not valid for the supplied Gregorian month and year.",
                         "day");
        } catch (const std::out_of_range&) {
            return Error(ErrorCode::unsupported_date,
                         "Date is outside Boost Gregorian's supported range.",
                         parsed.hasYear ? "year" : "date");
        }
    }
    return Error();
}

std::string fourDigits(int value) {
    std::ostringstream output;
    output << std::setw(4) << std::setfill('0') << value;
    return output.str();
}

std::string twoDigits(int value) {
    std::ostringstream output;
    output << std::setw(2) << std::setfill('0') << value;
    return output.str();
}

Error requiredFieldError(const char* field) {
    return Error(ErrorCode::missing_required_field,
                 std::string("Expiration cycle requires maturity ") + field + ".",
                 field);
}

} // namespace

Error::Error()
    : code(ErrorCode::invalid_configuration), message(), field(), position(0),
      hasPosition(false) {}

Error::Error(ErrorCode errorCode, const std::string& diagnostic,
             const std::string& fieldName)
    : code(errorCode), message(diagnostic), field(fieldName), position(0),
      hasPosition(false) {}

Error::Error(ErrorCode errorCode, const std::string& diagnostic,
             const std::string& fieldName, std::size_t inputPosition)
    : code(errorCode), message(diagnostic), field(fieldName),
      position(inputPosition), hasPosition(true) {}

Decimal::Decimal() : coefficient(0), scale(0) {}

Decimal::Decimal(std::uint64_t value, unsigned int decimalPlaces)
    : coefficient(value), scale(decimalPlaces) {}

std::string Decimal::toString() const {
    std::ostringstream output;
    output << coefficient;
    std::string digits = output.str();
    if (scale == 0) {
        return digits;
    }
    if (digits.size() <= scale) {
        digits.insert(0, scale + 1 - digits.size(), '0');
    }
    digits.insert(digits.size() - scale, 1, '.');
    return digits;
}

FieldSpec::FieldSpec()
    : kind(FieldKind::ticker), name(), position(kNoPosition),
      hasPosition(false), minLength(1), maxLength(1), literal(),
      allowedCharacters(), allowedValues(), mapping(), yearDigits(0),
      decimalEncoding(DecimalEncoding::explicitDecimal),
      impliedDecimalPlaces(0) {}

FieldSpec FieldSpec::literalField(const std::string& value) {
    FieldSpec result;
    result.kind = FieldKind::literal;
    result.name = "literal";
    result.literal = value;
    result.minLength = value.size();
    result.maxLength = value.size();
    return result;
}

FieldSpec FieldSpec::field(FieldKind fieldKind, const std::string& fieldName,
                           std::size_t minimumLength,
                           std::size_t maximumLength) {
    FieldSpec result;
    result.kind = fieldKind;
    result.name = fieldName;
    result.minLength = minimumLength;
    result.maxLength = maximumLength;
    return result;
}

FormatConfig::FormatConfig() : fields(), requireYearAndMonth(false) {}

FormatConfig FormatConfig::defaultFutures() {
    const std::string tickerCharacters =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-";
    FormatConfig format;
    FieldSpec ticker = FieldSpec::field(FieldKind::ticker, "ticker", 1, 32);
    ticker.allowedCharacters = tickerCharacters;
    format.fields.push_back(ticker);

    FieldSpec month = FieldSpec::field(FieldKind::month, "month", 1, 1);
    const char* codes[] = {"F", "G", "H", "J", "K", "M",
                           "N", "Q", "U", "V", "X", "Z"};
    for (int i = 0; i < 12; ++i) {
        month.mapping[codes[i]] = i + 1;
    }
    format.fields.push_back(month);

    FieldSpec year = FieldSpec::field(FieldKind::year, "year", 1, 2);
    year.allowedCharacters = "0123456789";
    format.fields.push_back(year);
    format.requireYearAndMonth = true;
    return format;
}

FormatConfig FormatConfig::defaultOptions() {
    FormatConfig format = defaultFutures();
    FieldSpec optionType =
        FieldSpec::field(FieldKind::optionType, "optionType", 1, 1);
    optionType.mapping["C"] = 1;
    optionType.mapping["P"] = 2;
    format.fields.push_back(optionType);

    FieldSpec strike = FieldSpec::field(FieldKind::strike, "strike", 1, 128);
    strike.decimalEncoding = DecimalEncoding::explicitDecimal;
    format.fields.push_back(strike);
    return format;
}

YearResolution::YearResolution()
    : referenceYear(0), minimumOffset(0), maximumOffset(0) {}

YearResolution::YearResolution(int reference, int minimumInclusiveOffset,
                               int maximumInclusiveOffset)
    : referenceYear(reference), minimumOffset(minimumInclusiveOffset),
      maximumOffset(maximumInclusiveOffset) {}

ParsedContract::ParsedContract()
    : instrument(InstrumentKind::future), hasTicker(false), ticker(),
      hasYear(false), year(0), hasMonth(false), month(0), hasDay(false), day(0),
      hasOptionType(false), optionType('\0'), hasStrike(false), strike(),
      metadata() {}

ContractTerm::ContractTerm(const std::string& value) : value_(value) {}

const std::string& ContractTerm::value() const { return value_; }

Result<ExpirationCycle> expirationCycleFromCode(const std::string& code) {
    if (code == "Y") {
        return Result<ExpirationCycle>::success(ExpirationCycle::yearly);
    }
    if (code == "H") {
        return Result<ExpirationCycle>::success(ExpirationCycle::halfYearly);
    }
    if (code == "Q") {
        return Result<ExpirationCycle>::success(ExpirationCycle::quarterly);
    }
    if (code == "B") {
        return Result<ExpirationCycle>::success(ExpirationCycle::biMonthly);
    }
    if (code == "M") {
        return Result<ExpirationCycle>::success(ExpirationCycle::monthly);
    }
    if (code == "W") {
        return Result<ExpirationCycle>::success(ExpirationCycle::weekly);
    }
    if (code == "D") {
        return Result<ExpirationCycle>::success(ExpirationCycle::daily);
    }
    if (code == "r") {
        return Result<ExpirationCycle>::success(ExpirationCycle::rollingSpot);
    }
    return Result<ExpirationCycle>::failure(
        Error(ErrorCode::unsupported_cycle,
              "Unsupported expiration-cycle code; cycle codes are case-sensitive."));
}

Result<ParsedContract> parseSymbol(const std::string& symbol,
                                  const FormatConfig& format,
                                  InstrumentKind instrument,
                                  const YearResolution& yearResolution) {
    Error configurationError = validateFormat(format, instrument);
    if (!configurationError.message.empty()) {
        return Result<ParsedContract>::failure(configurationError);
    }
    if (yearResolution.minimumOffset > yearResolution.maximumOffset) {
        return Result<ParsedContract>::failure(
            Error(ErrorCode::invalid_configuration,
                  "Year-resolution minimum offset must not exceed maximum offset.",
                  "year"));
    }
    return SymbolParser(symbol, format, instrument, yearResolution).run();
}

Result<ContractTerm> buildContractTerm(const ParsedContract& parsed,
                                       ExpirationCycle cycle) {
    if (!parsed.hasTicker || parsed.ticker.empty()) {
        return Result<ContractTerm>::failure(
            Error(ErrorCode::missing_required_field,
                  "A nonempty ticker is required.", "ticker"));
    }
    if (parsed.instrument == InstrumentKind::option &&
        (!parsed.hasOptionType || !parsed.hasStrike)) {
        return Result<ContractTerm>::failure(
            Error(ErrorCode::missing_required_field,
                  "Options require option type and strike fields."));
    }

    Error dateError = validateMaturity(parsed);
    if (!dateError.message.empty()) {
        return Result<ContractTerm>::failure(dateError);
    }

    if (cycle != ExpirationCycle::rollingSpot && !parsed.hasYear) {
        return Result<ContractTerm>::failure(requiredFieldError("year"));
    }

    std::string term;
    switch (cycle) {
    case ExpirationCycle::yearly:
        term = fourDigits(parsed.year);
        break;
    case ExpirationCycle::halfYearly:
        if (!parsed.hasMonth) {
            return Result<ContractTerm>::failure(requiredFieldError("month"));
        }
        term = fourDigits(parsed.year) + (parsed.month <= 6 ? "H1" : "H2");
        break;
    case ExpirationCycle::quarterly:
        if (!parsed.hasMonth) {
            return Result<ContractTerm>::failure(requiredFieldError("month"));
        }
        term = fourDigits(parsed.year) + "Q" +
               std::string(1, static_cast<char>('1' + (parsed.month - 1) / 3));
        break;
    case ExpirationCycle::biMonthly:
    case ExpirationCycle::monthly:
        if (!parsed.hasMonth) {
            return Result<ContractTerm>::failure(requiredFieldError("month"));
        }
        term = fourDigits(parsed.year) + "M" + twoDigits(parsed.month);
        break;
    case ExpirationCycle::weekly: {
        if (!parsed.hasMonth) {
            return Result<ContractTerm>::failure(requiredFieldError("month"));
        }
        if (!parsed.hasDay) {
            return Result<ContractTerm>::failure(
                Error(ErrorCode::missing_maturity_day,
                      "Weekly expiration requires an explicit maturity day in the symbol.",
                      "day"));
        }
        try {
            const boost::gregorian::date maturity(
                static_cast<unsigned short>(parsed.year),
                static_cast<unsigned short>(parsed.month),
                static_cast<unsigned short>(parsed.day));
            const unsigned short isoYear =
                (maturity + boost::gregorian::days(
                                4 - (maturity.day_of_week().as_number() == 0
                                         ? 7
                                         : maturity.day_of_week().as_number())))
                    .year();
            const unsigned short week = maturity.week_number();
            term = fourDigits(isoYear) + "W" + twoDigits(week);
        } catch (const boost::gregorian::bad_year&) {
            return Result<ContractTerm>::failure(
                Error(ErrorCode::unsupported_date,
                      "ISO week-year is outside Boost Gregorian's supported range.",
                      "year"));
        } catch (const boost::gregorian::bad_month&) {
            return Result<ContractTerm>::failure(
                Error(ErrorCode::invalid_date, "Invalid Gregorian month.", "month"));
        } catch (const boost::gregorian::bad_day_of_month&) {
            return Result<ContractTerm>::failure(
                Error(ErrorCode::invalid_date,
                      "Day is not valid for the supplied Gregorian month and year.",
                      "day"));
        } catch (const std::out_of_range&) {
            return Result<ContractTerm>::failure(
                Error(ErrorCode::unsupported_date,
                      "ISO week calculation is outside Boost Gregorian's supported range.",
                      "year"));
        }
        break;
    }
    case ExpirationCycle::daily:
        if (!parsed.hasMonth) {
            return Result<ContractTerm>::failure(requiredFieldError("month"));
        }
        if (!parsed.hasDay) {
            return Result<ContractTerm>::failure(
                Error(ErrorCode::missing_maturity_day,
                      "Daily expiration requires an explicit maturity day in the symbol.",
                      "day"));
        }
        term = fourDigits(parsed.year) + "M" + twoDigits(parsed.month) + "D" +
               twoDigits(parsed.day);
        break;
    case ExpirationCycle::rollingSpot:
        term = "ROLL";
        break;
    default:
        return Result<ContractTerm>::failure(
            Error(ErrorCode::unsupported_cycle,
                  "Expiration-cycle value is not supported."));
    }

    return Result<ContractTerm>::success(ContractTerm(term));
}

} // namespace COMP
} // namespace FH
