# Contract-term component

This standalone C++14 component parses futures and options symbols into
structured fields and then derives a normalized maturity-period term. Parsing
and term construction are separate operations: a caller supplies the
instrument kind, year-resolution policy, and expiration cycle explicitly.

## Build and run

The component uses Boost.Date_Time's Gregorian date type and requires Boost
headers. It has no external test framework dependency.

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

For a single-configuration generator, omit `--config Release`. CMake's
`Boost_INCLUDE_DIRS` can be supplied through `CMAKE_PREFIX_PATH` or the normal
Boost discovery environment variables when Boost is not installed in a
standard location.

The repository supplied for this component does not contain
`fh\util\MarketCalendar\MarketCalendarTypes.h` or any existing project build
system. To avoid fabricating that unavailable integration header, the
standalone implementation uses `boost::gregorian::date` directly. If the
component is moved into a project where that header is available, use its
declared date alias at the integration boundary; this implementation does not
redeclare the project's `GregorianDate` or `GregorianDateTime` typedefs.

## Public API and examples

Include `ContractTerm.h`. `FormatConfig` describes the symbol syntax,
`parseSymbol` returns a `ParsedContract`, and `buildContractTerm` creates a
`ContractTerm` from the parsed maturity. All expected failures are returned as
`Result<T>` with an `ErrorCode`, diagnostic, optional field name, and optional
zero-based input position. `Result::value()` is valid only when `hasValue()`
is true.

Default futures and options have the shapes
`[Ticker][MonthCode][YearCode]` and
`[Ticker][MonthCode][YearCode][CallPut][Strike]`. Default ticker characters
are uppercase ASCII letters, digits, period, underscore, and hyphen. Tickers
are 1-32 characters. The parser enumerates possible boundaries and rejects
conflicting valid interpretations rather than taking a greedy split.

```cpp
#include "ContractTerm.h"

using namespace FH::COMP;

YearResolution years(2026, -5, 5); // Inclusive window: 2021 through 2031.
Result<ParsedContract> parsed = parseSymbol(
    "ESH26", FormatConfig::defaultFutures(),
    InstrumentKind::future, years);
if (parsed) {
    Result<ContractTerm> term =
        buildContractTerm(parsed.value(), ExpirationCycle::monthly);
    if (term) {
        // term.value().value() == "2026M03"
    }
}

Result<ParsedContract> option = parseSymbol(
    "ESH26C5000", FormatConfig::defaultOptions(),
    InstrumentKind::option, years);
// Option strike remains an exact decimal (coefficient 5000, scale 0).
```

Custom fields are supplied in parse order. Each field has a bounded length;
literal fields represent separators or prefixes. Positions, when supplied,
are absolute zero-based offsets and fields must still be listed in ascending
physical order. Explicit fixed-length fields may not overlap; bounded
variable-length fields are checked against the next position for each
candidate split. A gap between fields is not implicitly skipped: express it as
a literal, otherwise the input is rejected. Unpositioned fields begin
immediately after the preceding field.

The following custom format parses an explicit day, which is required for
weekly and daily terms. It also demonstrates four-digit years and numeric
month/day fields:

```cpp
FormatConfig dated;
FieldSpec ticker = FieldSpec::field(FieldKind::ticker, "ticker", 2, 2);
ticker.allowedCharacters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
dated.fields.push_back(ticker);
dated.fields.push_back(FieldSpec::literalField("-"));

FieldSpec year = FieldSpec::field(FieldKind::year, "year", 4, 4);
year.yearDigits = 4;
year.allowedCharacters = "0123456789";
dated.fields.push_back(year);
dated.fields.push_back(FieldSpec::literalField("-"));

FieldSpec month = FieldSpec::field(FieldKind::month, "month", 2, 2);
month.allowedCharacters = "0123456789";
dated.fields.push_back(month);
dated.fields.push_back(FieldSpec::literalField("-"));

FieldSpec day = FieldSpec::field(FieldKind::day, "day", 2, 2);
day.allowedCharacters = "0123456789";
dated.fields.push_back(day);

Result<ParsedContract> daily = parseSymbol(
    "ES-2024-02-29", dated, InstrumentKind::future, years);
// On success, buildContractTerm(daily.value(), ExpirationCycle::daily)
// returns "2024M02D29".
```

Use a custom format with no maturity fields for rolling spot:

```cpp
FormatConfig rolling;
FieldSpec ticker = FieldSpec::field(FieldKind::ticker, "ticker", 1, 8);
ticker.allowedCharacters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
rolling.fields.push_back(ticker);

Result<ParsedContract> spot = parseSymbol(
    "ES", rolling, InstrumentKind::future, years);
// On success, buildContractTerm(spot.value(), ExpirationCycle::rollingSpot)
// returns "ROLL".
```

Mappings are configured on a field with `FieldSpec::mapping`, using strings
as encoded input values. Month mappings must map to integers 1-12. Option-type
mappings use 1 for call and 2 for put. When the month mapping is empty, month
fields are interpreted as decimal integers; day fields are decimal integers.
`allowedCharacters` constrains individual characters and `allowedValues`
constrains the complete field value. Metadata fields retain their exact string
value under their unique configured name.

Strike values use `Decimal`, an unsigned 64-bit coefficient and a decimal
scale; no binary floating-point conversion occurs. Explicit decimal encoding
preserves the number of fractional digits written (including trailing zeroes).
Implied-decimal encoding treats all input digits as the coefficient and uses
the configured scale. Precision is limited to 18 fractional places and
coefficients are limited to `uint64_t`; a leading sign, exponent, thousands
separator, or more than one decimal point is not supported. Explicit decimal
syntax requires digits before the decimal point and, when a point is present,
at least one fractional digit.

## Year resolution and dates

`YearResolution(referenceYear, minimumOffset, maximumOffset)` defines the
inclusive permitted interval
`[referenceYear + minimumOffset, referenceYear + maximumOffset]`. One- and
two-digit year suffixes are matched against the final one or two digits of
each supported year in that interval. A unique match is accepted; no match
and multiple matches have distinct errors. Four-digit year fields are
absolute years and do not use this window. No system clock or implicit
century/decade inference is used.

All supplied year, month, and day values are validated even when a cycle does
not need every one. The supported Gregorian year range is 1400-9999,
inclusive, matching Boost Gregorian. Leap days and month lengths are checked
through Boost date construction; expected Boost invalid-date exceptions are
converted to structured errors. Missing fields remain absent in
`ParsedContract`: for example, a year/month value does not fabricate a day.
Weekly and daily cycles require a day explicitly present in the symbol. No
exchange expiry calendar or external maturity override is applied.

Weekly output uses ISO 8601 week numbering and ISO week-year, not simply the
calendar year. Week 1 contains January 4 and weeks begin Monday. Rolling spot
always produces `ROLL`; it does not require maturity data.

Supported cycle codes are case-sensitive: `Y`, `H`, `Q`, `B`, `M`, `W`, `D`,
and `r`. Contract terms contain only the normalized maturity period, never
ticker, strike, option type, or metadata.

## Format validation and error behavior

Formats must contain exactly one ticker. Options additionally require exactly
one option-type and strike field; futures may not declare those option fields.
Duplicate semantic fields, duplicate/empty metadata names, invalid bounds,
invalid mappings, overlapping explicit positions, and oversized parse-split
spaces are rejected as configuration errors. Field bounds are inclusive and
must satisfy `1 <= minimum <= maximum <= 256`. A format may contain at most
128 fields and may yield at most one million bounded length combinations.

Every input character must be consumed by a field or literal. Variable-length
fields are exhaustively considered within their configured bounds. If multiple
successful splits produce different extracted data, parsing returns an
ambiguity error. A symbol is limited to 4096 characters. Failures never return
a partially valid `ContractTerm`; inspect `ErrorCode`, `field`, and
`hasPosition`/`position` for machine-readable diagnostics.
