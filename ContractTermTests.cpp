#include "ContractTerm.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

using FH::COMP::ContractTerm;
using FH::COMP::DecimalEncoding;
using FH::COMP::ErrorCode;
using FH::COMP::ExpirationCycle;
using FH::COMP::FieldKind;
using FH::COMP::FieldSpec;
using FH::COMP::FormatConfig;
using FH::COMP::InstrumentKind;
using FH::COMP::ParsedContract;
using FH::COMP::Result;
using FH::COMP::YearResolution;

namespace {

int failures = 0;

void check(bool condition, const std::string& label) {
    if (!condition) {
        std::cerr << "FAIL: " << label << '\n';
        ++failures;
    }
}

template <typename T>
void expectError(const Result<T>& result, ErrorCode code,
                 const std::string& label) {
    check(!result.hasValue(), label + " returns an error");
    if (!result.hasValue()) {
        check(result.error().code == code, label + " error code");
        check(!result.error().message.empty(), label + " diagnostic");
    }
}

std::string termFor(const std::string& symbol, const FormatConfig& format,
                    InstrumentKind instrument, const YearResolution& years,
                    ExpirationCycle cycle) {
    Result<ParsedContract> parsed =
        FH::COMP::parseSymbol(symbol, format, instrument, years);
    if (!parsed) {
        check(false, "parse " + symbol + ": " + parsed.error().message);
        return std::string();
    }
    Result<ContractTerm> term =
        FH::COMP::buildContractTerm(parsed.value(), cycle);
    if (!term) {
        check(false, "build term " + symbol + ": " + term.error().message);
        return std::string();
    }
    return term.value().value();
}

FormatConfig datedFormat() {
    FormatConfig format;
    FieldSpec ticker = FieldSpec::field(FieldKind::ticker, "ticker", 2, 2);
    ticker.allowedCharacters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    ticker.hasPosition = true;
    ticker.position = 0;
    format.fields.push_back(ticker);

    FieldSpec firstSeparator = FieldSpec::literalField("-");
    firstSeparator.hasPosition = true;
    firstSeparator.position = 2;
    format.fields.push_back(firstSeparator);

    FieldSpec year = FieldSpec::field(FieldKind::year, "year", 4, 4);
    year.yearDigits = 4;
    year.allowedCharacters = "0123456789";
    year.hasPosition = true;
    year.position = 3;
    format.fields.push_back(year);

    FieldSpec secondSeparator = FieldSpec::literalField("-");
    secondSeparator.hasPosition = true;
    secondSeparator.position = 7;
    format.fields.push_back(secondSeparator);

    FieldSpec month = FieldSpec::field(FieldKind::month, "month", 2, 2);
    month.allowedCharacters = "0123456789";
    month.hasPosition = true;
    month.position = 8;
    format.fields.push_back(month);

    FieldSpec thirdSeparator = FieldSpec::literalField("-");
    thirdSeparator.hasPosition = true;
    thirdSeparator.position = 10;
    format.fields.push_back(thirdSeparator);

    FieldSpec day = FieldSpec::field(FieldKind::day, "day", 2, 2);
    day.allowedCharacters = "0123456789";
    day.hasPosition = true;
    day.position = 11;
    format.fields.push_back(day);
    return format;
}

FormatConfig rollingFormat() {
    FormatConfig format;
    FieldSpec ticker = FieldSpec::field(FieldKind::ticker, "ticker", 1, 8);
    ticker.allowedCharacters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    format.fields.push_back(ticker);
    return format;
}

void testCyclesAndDefaults() {
    const YearResolution years(2026, -2, 2);
    const FormatConfig futures = FormatConfig::defaultFutures();
    const FormatConfig options = FormatConfig::defaultOptions();

    check(termFor("ESH26", futures, InstrumentKind::future, years,
                  ExpirationCycle::halfYearly) == "2026H1",
          "default future and H1");
    check(termFor("ESN26", futures, InstrumentKind::future, years,
                  ExpirationCycle::halfYearly) == "2026H2",
          "H2 boundary");
    check(termFor("ESM26", futures, InstrumentKind::future, years,
                  ExpirationCycle::halfYearly) == "2026H1",
          "June remains in the first half");
    check(termFor("ESJ26", futures, InstrumentKind::future, years,
                  ExpirationCycle::quarterly) == "2026Q2",
          "quarter boundary");
    check(termFor("ESH26", futures, InstrumentKind::future, years,
                  ExpirationCycle::monthly) == "2026M03",
          "month two digit format");
    check(termFor("ESH26", futures, InstrumentKind::future, years,
                  ExpirationCycle::biMonthly) == "2026M03",
          "bi-monthly has no bucketing");
    check(termFor("ESH26", futures, InstrumentKind::future, years,
                  ExpirationCycle::yearly) == "2026",
          "yearly format");

    Result<ParsedContract> option =
        FH::COMP::parseSymbol("ESH26C5000", options, InstrumentKind::option,
                              years);
    check(option.hasValue(), "default option parses");
    if (option) {
        check(option.value().optionType == 'C', "call mapping");
        check(option.value().strike.toString() == "5000", "integer strike");
        check(FH::COMP::buildContractTerm(option.value(),
                                          ExpirationCycle::monthly)
                  .value()
                  .value() == "2026M03",
              "option term excludes option metadata");
    }

    const char* monthCodes[] = {"F", "G", "H", "J", "K", "M",
                                "N", "Q", "U", "V", "X", "Z"};
    for (int month = 1; month <= 12; ++month) {
        const std::string symbol =
            std::string("T") + monthCodes[month - 1] + "26";
        Result<ParsedContract> parsed = FH::COMP::parseSymbol(
            symbol, futures, InstrumentKind::future, years);
        check(parsed.hasValue(), "all default month codes parse");
        if (parsed) {
            check(parsed.value().month == month, "default month mapping value");
        }
    }
}

void testYearResolution() {
    const FormatConfig futures = FormatConfig::defaultFutures();
    Result<ParsedContract> oneDigit = FH::COMP::parseSymbol(
        "ESH6", futures, InstrumentKind::future, YearResolution(2026, -1, 1));
    check(oneDigit && oneDigit.value().year == 2026, "one-digit year");

    Result<ParsedContract> lowerBoundary = FH::COMP::parseSymbol(
        "ESH24", futures, InstrumentKind::future, YearResolution(2026, -2, 2));
    Result<ParsedContract> upperBoundary = FH::COMP::parseSymbol(
        "ESH28", futures, InstrumentKind::future, YearResolution(2026, -2, 2));
    check(lowerBoundary && lowerBoundary.value().year == 2024,
          "inclusive lower year-window boundary");
    check(upperBoundary && upperBoundary.value().year == 2028,
          "inclusive upper year-window boundary");

    expectError(FH::COMP::parseSymbol("ESH23", futures,
                                      InstrumentKind::future,
                                      YearResolution(2026, -2, 2)),
                ErrorCode::year_resolution_failed, "year outside window");
    expectError(FH::COMP::parseSymbol("ESH26", futures,
                                      InstrumentKind::future,
                                      YearResolution(2026, -100, 100)),
                ErrorCode::ambiguous_year, "ambiguous short year");

    Result<ParsedContract> fourDigit = FH::COMP::parseSymbol(
        "ES-2024-02-29", datedFormat(), InstrumentKind::future,
        YearResolution(0, 0, 0));
    check(fourDigit && fourDigit.value().year == 2024,
          "four-digit custom year bypasses short-year window");
    expectError(FH::COMP::parseSymbol("ES-1399-02-28", datedFormat(),
                                     InstrumentKind::future,
                                     YearResolution(2026, -2, 2)),
                ErrorCode::unsupported_date, "Boost Gregorian lower year limit");
}

void testDatesAndWeeks() {
    const YearResolution years(2026, -2, 2);
    const FormatConfig dates = datedFormat();
    check(termFor("ES-2021-01-01", dates, InstrumentKind::future, years,
                  ExpirationCycle::weekly) == "2020W53",
          "ISO week-year before calendar year");
    check(termFor("ES-2024-12-30", dates, InstrumentKind::future, years,
                  ExpirationCycle::weekly) == "2025W01",
          "ISO week-year after calendar year");
    check(termFor("ES-2024-02-29", dates, InstrumentKind::future, years,
                  ExpirationCycle::daily) == "2024M02D29",
          "leap-day daily term");
    check(termFor("ES-9999-12-31", dates, InstrumentKind::future, years,
                  ExpirationCycle::daily) == "9999M12D31",
          "upper Boost Gregorian year boundary");
    check(termFor("ES-2020-12-31", dates, InstrumentKind::future, years,
                  ExpirationCycle::weekly) == "2020W53",
          "valid ISO week 53");

    Result<ParsedContract> invalidLeap = FH::COMP::parseSymbol(
        "ES-2023-02-29", dates, InstrumentKind::future, years);
    check(invalidLeap.hasValue(), "date syntax is parsed before validation");
    if (invalidLeap) {
        expectError(FH::COMP::buildContractTerm(invalidLeap.value(),
                                                ExpirationCycle::daily),
                    ErrorCode::invalid_date, "invalid leap date");
    }

    Result<ParsedContract> noDay = FH::COMP::parseSymbol(
        "ESH26", FormatConfig::defaultFutures(), InstrumentKind::future, years);
    if (noDay) {
        expectError(FH::COMP::buildContractTerm(noDay.value(),
                                               ExpirationCycle::weekly),
                    ErrorCode::missing_maturity_day, "weekly requires maturity day");
        expectError(FH::COMP::buildContractTerm(noDay.value(),
                                               ExpirationCycle::daily),
                    ErrorCode::missing_maturity_day, "daily requires maturity day");
    }

    Result<ParsedContract> badDate = FH::COMP::parseSymbol(
        "ES-2024-13-01", dates, InstrumentKind::future, years);
    expectError(badDate, ErrorCode::invalid_date,
                "invalid month translated to structured result");
}

void testCustomFormatsAndMetadata() {
    const YearResolution years(2026, -2, 2);
    const FormatConfig dates = datedFormat();
    Result<ParsedContract> custom = FH::COMP::parseSymbol(
        "ES-2024-12-30", dates, InstrumentKind::future, years);
    check(custom.hasValue(), "explicit positions and separators parse");
    if (custom) {
        check(custom.value().hasDay && custom.value().day == 30,
              "custom maturity day extracted");
    }

    FormatConfig serialOption = FormatConfig::defaultOptions();
    FieldSpec serial = FieldSpec::field(FieldKind::metadata, "serial", 1, 1);
    serial.allowedCharacters = "SN";
    serialOption.fields.push_back(FieldSpec::literalField("-"));
    serialOption.fields.push_back(serial);
    Result<ParsedContract> metadata = FH::COMP::parseSymbol(
        "ESH26C5000-S", serialOption, InstrumentKind::option, years);
    check(metadata.hasValue(), "custom serial option format parses");
    if (metadata) {
        check(metadata.value().metadata["serial"] == "S",
              "serial metadata is retained");
    }

    check(termFor("ES", rollingFormat(), InstrumentKind::future, years,
                  ExpirationCycle::rollingSpot) == "ROLL",
          "rolling spot with no maturity fields");

    FormatConfig reordered;
    FieldSpec year = FieldSpec::field(FieldKind::year, "year", 4, 4);
    year.yearDigits = 4;
    year.allowedCharacters = "0123456789";
    year.hasPosition = true;
    year.position = 0;
    reordered.fields.push_back(year);
    FieldSpec separator = FieldSpec::literalField("-");
    separator.hasPosition = true;
    separator.position = 4;
    reordered.fields.push_back(separator);
    FieldSpec ticker = FieldSpec::field(FieldKind::ticker, "ticker", 2, 2);
    ticker.allowedCharacters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    ticker.hasPosition = true;
    ticker.position = 5;
    reordered.fields.push_back(ticker);
    check(termFor("2026-ES", reordered, InstrumentKind::future, years,
                  ExpirationCycle::yearly) == "2026",
          "custom fields may be ordered independently of conventional symbols");

    Result<ExpirationCycle> lowercaseRolling =
        FH::COMP::expirationCycleFromCode("r");
    check(lowercaseRolling && lowercaseRolling.value() ==
                                  ExpirationCycle::rollingSpot,
          "case-sensitive rolling code");
    const char* cycleCodes[] = {"Y", "H", "Q", "B", "M", "W", "D"};
    for (std::size_t i = 0; i < sizeof(cycleCodes) / sizeof(cycleCodes[0]); ++i) {
        check(FH::COMP::expirationCycleFromCode(cycleCodes[i]).hasValue(),
              "supported cycle code");
    }
    expectError(FH::COMP::expirationCycleFromCode("R"),
                ErrorCode::unsupported_cycle, "uppercase rolling code rejected");

    FormatConfig mappedOption;
    FieldSpec mappedTicker =
        FieldSpec::field(FieldKind::ticker, "ticker", 2, 2);
    mappedTicker.allowedCharacters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    mappedOption.fields.push_back(mappedTicker);
    FieldSpec mappedMonth =
        FieldSpec::field(FieldKind::month, "month", 1, 1);
    mappedMonth.mapping["a"] = 12;
    mappedOption.fields.push_back(mappedMonth);
    FieldSpec mappedYear =
        FieldSpec::field(FieldKind::year, "year", 4, 4);
    mappedYear.yearDigits = 4;
    mappedOption.fields.push_back(mappedYear);
    FieldSpec mappedType =
        FieldSpec::field(FieldKind::optionType, "optionType", 1, 1);
    mappedType.mapping["1"] = 1;
    mappedOption.fields.push_back(mappedType);
    FieldSpec mappedStrike =
        FieldSpec::field(FieldKind::strike, "strike", 1, 12);
    mappedOption.fields.push_back(mappedStrike);
    Result<ParsedContract> customOption = FH::COMP::parseSymbol(
        "ESa20261100", mappedOption, InstrumentKind::option, years);
    check(customOption && customOption.value().month == 12,
          "custom month mapping and reordered option code");
}

void testStrikesAndFailures() {
    const YearResolution years(2026, -2, 2);
    const FormatConfig options = FormatConfig::defaultOptions();
    Result<ParsedContract> decimal = FH::COMP::parseSymbol(
        "ESH26P12.340", options, InstrumentKind::option, years);
    check(decimal && decimal.value().optionType == 'P',
          "put option and decimal strike parse");
    if (decimal) {
        check(decimal.value().strike.toString() == "12.340",
              "explicit strike precision retained");
    }

    FormatConfig implied = FormatConfig::defaultOptions();
    implied.fields.back().decimalEncoding = DecimalEncoding::impliedDecimal;
    implied.fields.back().impliedDecimalPlaces = 2;
    Result<ParsedContract> scaled = FH::COMP::parseSymbol(
        "ESH26C12345", implied, InstrumentKind::option, years);
    check(scaled && scaled.value().strike.toString() == "123.45",
          "implied decimal strike scale");

    expectError(FH::COMP::parseSymbol(
                    "ESH26C18446744073709551616", options,
                    InstrumentKind::option, years),
                ErrorCode::numeric_overflow, "strike coefficient overflow");
    expectError(FH::COMP::parseSymbol("ESH26C1.1234567890123456789",
                                      options, InstrumentKind::option, years),
                ErrorCode::unsupported_precision, "unsupported strike precision");

    FormatConfig duplicate = FormatConfig::defaultFutures();
    duplicate.fields.push_back(
        FieldSpec::field(FieldKind::month, "anotherMonth", 1, 1));
    expectError(FH::COMP::parseSymbol("ESH26", duplicate,
                                      InstrumentKind::future, years),
                ErrorCode::invalid_configuration, "duplicate semantic field");

    FormatConfig overlap = datedFormat();
    overlap.fields.back().position = 10;
    expectError(FH::COMP::parseSymbol("ES-2024-02-29", overlap,
                                      InstrumentKind::future, years),
                ErrorCode::invalid_configuration, "overlapping explicit positions");

    Result<ParsedContract> trailing = FH::COMP::parseSymbol(
        "ES-2024-02-29X", datedFormat(), InstrumentKind::future, years);
    check(!trailing.hasValue(), "unexpected trailing input rejected");
    if (!trailing) {
        check(trailing.error().code == ErrorCode::unconsumed_input,
              "trailing input has structured unconsumed-input error");
    }

    expectError(FH::COMP::parseSymbol(
                    "ESH26C1", FormatConfig::defaultOptions(),
                    InstrumentKind::future, years),
                ErrorCode::invalid_configuration,
                "future cannot use option fields");
}

void testAmbiguousBoundaries() {
    FormatConfig format;
    FieldSpec ticker = FieldSpec::field(FieldKind::ticker, "ticker", 1, 3);
    ticker.allowedCharacters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    format.fields.push_back(ticker);
    FieldSpec month = FieldSpec::field(FieldKind::month, "month", 1, 1);
    month.mapping["F"] = 1;
    month.mapping["2"] = 2;
    format.fields.push_back(month);
    FieldSpec year = FieldSpec::field(FieldKind::year, "year", 1, 2);
    year.allowedCharacters = "0123456789";
    format.fields.push_back(year);
    expectError(FH::COMP::parseSymbol("ABF26", format,
                                      InstrumentKind::future,
                                      YearResolution(2026, -1, 1)),
                ErrorCode::ambiguous_symbol,
                "different valid variable-length splits are ambiguous");
}

void testPartialMaturityAndCycleRequirements() {
    const YearResolution years(2026, -2, 2);
    FormatConfig yearOnly;
    FieldSpec ticker = FieldSpec::field(FieldKind::ticker, "ticker", 1, 2);
    ticker.allowedCharacters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    yearOnly.fields.push_back(ticker);
    yearOnly.fields.push_back(
        FieldSpec::literalField("-"));
    FieldSpec year = FieldSpec::field(FieldKind::year, "year", 4, 4);
    year.yearDigits = 4;
    year.allowedCharacters = "0123456789";
    yearOnly.fields.push_back(year);

    Result<ParsedContract> parsed = FH::COMP::parseSymbol(
        "E-2026", yearOnly, InstrumentKind::future, years);
    check(parsed && !parsed.value().hasMonth && !parsed.value().hasDay,
          "missing maturity fields remain absent");
    if (parsed) {
        check(FH::COMP::buildContractTerm(parsed.value(),
                                          ExpirationCycle::yearly)
                  .value()
                  .value() == "2026",
              "yearly cycle only needs a year");
        expectError(FH::COMP::buildContractTerm(parsed.value(),
                                               ExpirationCycle::monthly),
                    ErrorCode::missing_required_field,
                    "monthly cycle requires a month");
    }

    FormatConfig monthOnly = rollingFormat();
    monthOnly.fields.push_back(FieldSpec::literalField("-"));
    FieldSpec month = FieldSpec::field(FieldKind::month, "month", 2, 2);
    month.allowedCharacters = "0123456789";
    monthOnly.fields.push_back(month);
    Result<ParsedContract> rollingMonth = FH::COMP::parseSymbol(
        "ES-02", monthOnly, InstrumentKind::future, years);
    check(rollingMonth && !rollingMonth.value().hasYear &&
              rollingMonth.value().month == 2,
          "partial maturity is retained for rolling spot");
    if (rollingMonth) {
        check(FH::COMP::buildContractTerm(rollingMonth.value(),
                                          ExpirationCycle::rollingSpot)
                  .value()
                  .value() == "ROLL",
              "rolling term ignores valid partial maturity");
    }
}

} // namespace

int main() {
    testCyclesAndDefaults();
    testYearResolution();
    testDatesAndWeeks();
    testCustomFormatsAndMetadata();
    testStrikesAndFailures();
    testAmbiguousBoundaries();
    testPartialMaturityAndCycleRequirements();
    if (failures != 0) {
        std::cerr << failures << " test assertion(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "All ContractTerm tests passed.\n";
    return EXIT_SUCCESS;
}
