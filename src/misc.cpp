#include "misc.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

// Number <-> text conversions used by the cache natives, mysql_format and the
// ORM. They reproduce Boost.Spirit's qi::int_, qi::double_ and karma's
// int_generator / double_ (Boost 1.71) exactly, which earlier versions of the
// plugin used, so scripts see byte-identical results without the Boost
// dependency. Parsing accepts a valid prefix ("12abc" -> 12) and never skips
// whitespace.

namespace
{
	// Same literals as Spirit's pow10 table, so scaling rounds identically.
	const double Pow10Table[] =
	{
	1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9,
	1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19,
	1e20, 1e21, 1e22, 1e23, 1e24, 1e25, 1e26, 1e27, 1e28, 1e29,
	1e30, 1e31, 1e32, 1e33, 1e34, 1e35, 1e36, 1e37, 1e38, 1e39,
	1e40, 1e41, 1e42, 1e43, 1e44, 1e45, 1e46, 1e47, 1e48, 1e49,
	1e50, 1e51, 1e52, 1e53, 1e54, 1e55, 1e56, 1e57, 1e58, 1e59,
	1e60, 1e61, 1e62, 1e63, 1e64, 1e65, 1e66, 1e67, 1e68, 1e69,
	1e70, 1e71, 1e72, 1e73, 1e74, 1e75, 1e76, 1e77, 1e78, 1e79,
	1e80, 1e81, 1e82, 1e83, 1e84, 1e85, 1e86, 1e87, 1e88, 1e89,
	1e90, 1e91, 1e92, 1e93, 1e94, 1e95, 1e96, 1e97, 1e98, 1e99,
	1e100, 1e101, 1e102, 1e103, 1e104, 1e105, 1e106, 1e107, 1e108, 1e109,
	1e110, 1e111, 1e112, 1e113, 1e114, 1e115, 1e116, 1e117, 1e118, 1e119,
	1e120, 1e121, 1e122, 1e123, 1e124, 1e125, 1e126, 1e127, 1e128, 1e129,
	1e130, 1e131, 1e132, 1e133, 1e134, 1e135, 1e136, 1e137, 1e138, 1e139,
	1e140, 1e141, 1e142, 1e143, 1e144, 1e145, 1e146, 1e147, 1e148, 1e149,
	1e150, 1e151, 1e152, 1e153, 1e154, 1e155, 1e156, 1e157, 1e158, 1e159,
	1e160, 1e161, 1e162, 1e163, 1e164, 1e165, 1e166, 1e167, 1e168, 1e169,
	1e170, 1e171, 1e172, 1e173, 1e174, 1e175, 1e176, 1e177, 1e178, 1e179,
	1e180, 1e181, 1e182, 1e183, 1e184, 1e185, 1e186, 1e187, 1e188, 1e189,
	1e190, 1e191, 1e192, 1e193, 1e194, 1e195, 1e196, 1e197, 1e198, 1e199,
	1e200, 1e201, 1e202, 1e203, 1e204, 1e205, 1e206, 1e207, 1e208, 1e209,
	1e210, 1e211, 1e212, 1e213, 1e214, 1e215, 1e216, 1e217, 1e218, 1e219,
	1e220, 1e221, 1e222, 1e223, 1e224, 1e225, 1e226, 1e227, 1e228, 1e229,
	1e230, 1e231, 1e232, 1e233, 1e234, 1e235, 1e236, 1e237, 1e238, 1e239,
	1e240, 1e241, 1e242, 1e243, 1e244, 1e245, 1e246, 1e247, 1e248, 1e249,
	1e250, 1e251, 1e252, 1e253, 1e254, 1e255, 1e256, 1e257, 1e258, 1e259,
	1e260, 1e261, 1e262, 1e263, 1e264, 1e265, 1e266, 1e267, 1e268, 1e269,
	1e270, 1e271, 1e272, 1e273, 1e274, 1e275, 1e276, 1e277, 1e278, 1e279,
	1e280, 1e281, 1e282, 1e283, 1e284, 1e285, 1e286, 1e287, 1e288, 1e289,
	1e290, 1e291, 1e292, 1e293, 1e294, 1e295, 1e296, 1e297, 1e298, 1e299,
	1e300, 1e301, 1e302, 1e303, 1e304, 1e305, 1e306, 1e307, 1e308
	};
	const int MaxExponent10 = std::numeric_limits<double>::max_exponent10; // 308
	const int MinExponent10 = std::numeric_limits<double>::min_exponent10; // -307

	inline double Pow10(int exp) { return Pow10Table[exp]; }
	inline bool IsDigit(char c) { return c >= '0' && c <= '9'; }

	// qi extract_uint<uint64, 10, 1, 17>: at most 17 digits, leading zeros
	// included, so it never overflows.
	bool ParseMantissa(const char *&first, const char *last, uint64_t &value)
	{
		const char *it = first;
		size_t count = 0;
		while (it != last && IsDigit(*it) && count < 17)
		{
			value = value * 10 + static_cast<uint64_t>(*it - '0');
			++it;
			++count;
		}
		if (count == 0)
			return false;
		first = it;
		return true;
	}

	// qi extract_uint<unused, 10, 1, -1>: skips a run of digits, returns how many.
	size_t SkipDigits(const char *&first, const char *last)
	{
		const char *it = first;
		while (it != last && IsDigit(*it))
			++it;
		const size_t count = static_cast<size_t>(it - first);
		first = it;
		return count;
	}

	// qi parse_frac_n: keeps accumulating into the mantissa until it would
	// overflow, then ignores the remaining digits.
	bool ParseFraction(const char *&first, const char *last, uint64_t &value, int &frac_digits)
	{
		if (first == last)
			return false;
		const char *it = first;
		const uint64_t max = std::numeric_limits<uint64_t>::max();
		while (it != last && IsDigit(*it))
		{
			const uint64_t digit = static_cast<uint64_t>(*it - '0');
			if (value > max / 10 || value * 10 > max - digit)
				break; // overflow: stop, the rest is skipped below
			value = value * 10 + digit;
			++it;
		}
		if (it == first && !(it != last && IsDigit(*it)))
			return false;
		frac_digits = static_cast<int>(it - first);
		first = it;
		SkipDigits(first, last);
		return true;
	}

	// qi extract_int<int, 10, 1, -1>: optional sign, fails on int overflow.
	bool ParseInt(const char *&first, const char *last, int &dest)
	{
		const char *it = first;
		if (it == last)
			return false;
		bool negative = false;
		if (*it == '-' || *it == '+')
		{
			negative = (*it == '-');
			++it;
		}
		const char *digits = it;
		long long value = 0;
		const long long limit = negative ? -static_cast<long long>(std::numeric_limits<int>::min())
			: static_cast<long long>(std::numeric_limits<int>::max());
		while (it != last && IsDigit(*it))
		{
			value = value * 10 + (*it - '0');
			if (value > limit)
				return false;
			++it;
		}
		if (it == digits)
			return false;
		dest = static_cast<int>(negative ? -value : value);
		first = it;
		return true;
	}

	// Case-insensitive literal match; advances only on success.
	bool MatchNoCase(const char *&first, const char *last, const char *lower)
	{
		const char *it = first;
		for (; *lower; ++lower, ++it)
		{
			if (it == last || (*it != *lower && *it != *lower - ('a' - 'A')))
				return false;
		}
		first = it;
		return true;
	}

	// qi traits::scale
	bool Scale(int exp, double &n, uint64_t acc_n)
	{
		if (exp >= 0)
		{
			if (exp > MaxExponent10)
				return false;
			n = static_cast<double>(acc_n) * Pow10(exp);
		}
		else if (exp < MinExponent10)
		{
			n = static_cast<double>((acc_n / 10) * 10);
			n += static_cast<double>(acc_n % 10);
			n /= Pow10(-MinExponent10);
			exp += -MinExponent10;
			if (exp < MinExponent10)
				return false;
			n /= Pow10(-exp);
		}
		else
			n = static_cast<double>(acc_n) / Pow10(-exp);
		return true;
	}

	// qi real_impl<double, real_policies<double>>::parse
	bool ParseDouble(const char *&first, const char *last, double &attr)
	{
		if (first == last)
			return false;
		const char *save = first;

		bool neg = false;
		if (*first == '-' || *first == '+')
		{
			neg = (*first == '-');
			++first;
		}

		double n = 0;
		uint64_t acc_n = 0;
		const bool got_a_number = ParseMantissa(first, last, acc_n);
		int excess_n = 0;

		if (!got_a_number)
		{
			if (first != last && (*first == 'n' || *first == 'N') && MatchNoCase(first, last, "nan"))
			{
				bool valid = true;
				if (first != last && *first == '(')
				{
					const char *i = first;
					while (++i != last && *i != ')')
						;
					if (i == last)
						valid = false;
					else
						first = ++i;
				}
				if (valid)
				{
					n = std::numeric_limits<double>::quiet_NaN();
					attr = neg ? -n : n;
					return true;
				}
			}
			if (first != last && (*first == 'i' || *first == 'I') && MatchNoCase(first, last, "inf"))
			{
				MatchNoCase(first, last, "inity");
				n = std::numeric_limits<double>::infinity();
				attr = neg ? -n : n;
				return true;
			}
		}
		else
			excess_n = static_cast<int>(SkipDigits(first, last));

		bool e_hit = false;
		const char *e_pos = NULL;
		int frac_digits = 0;

		if (first != last && *first == '.')
		{
			++first;
			if (excess_n != 0)
				SkipDigits(first, last);
			else if (ParseFraction(first, last, acc_n, frac_digits))
				;
			else if (!got_a_number)
			{
				first = save;
				return false;
			}
			e_pos = first;
			e_hit = (first != last && (*first == 'e' || *first == 'E'));
			if (e_hit) ++first;
		}
		else
		{
			if (!got_a_number)
			{
				first = save;
				return false;
			}
			e_pos = first;
			e_hit = (first != last && (*first == 'e' || *first == 'E'));
			if (e_hit) ++first;
		}

		if (e_hit)
		{
			int exp = 0;
			if (ParseInt(first, last, exp))
			{
				if (!Scale(exp + excess_n - frac_digits, n, acc_n))
					return false;
			}
			else
			{
				first = e_pos;
				Scale(-frac_digits, n, acc_n);
			}
		}
		else if (frac_digits)
			Scale(-frac_digits, n, acc_n);
		else if (excess_n)
		{
			if (!Scale(excess_n, n, acc_n))
				return false;
		}
		else
			n = static_cast<double>(acc_n);

		attr = neg ? -n : n;
		return true;
	}

	// karma int_inserter for an integral-valued double.
	char *WriteIntegral(char *dest, double n)
	{
		char digits[400];
		int count = 0;
		do
		{
			digits[count++] = static_cast<char>('0' + static_cast<int>(std::fmod(n, 10.0)));
			n = std::floor(n / 10.0);
		} while (n != 0 && count < static_cast<int>(sizeof(digits)));
		while (count > 0)
			*dest++ = digits[--count];
		return dest;
	}

	template<unsigned int B>
	char *WriteUnsigned(char *dest, unsigned int value)
	{
		char digits[33];
		int count = 0;
		do
		{
			const unsigned int digit = value % B;
			digits[count++] = static_cast<char>(digit < 10 ? '0' + digit : 'a' + digit - 10);
			value /= B;
		} while (value != 0);
		while (count > 0)
			*dest++ = digits[--count];
		return dest;
	}
}


bool ConvertStrToInt(const char *src, int &dest)
{
	if (src == NULL)
		return false;
	const char *first = src;
	return ParseInt(first, src + strlen(src), dest);
}

bool ConvertStrToFloat(const char *src, float &dest)
{
	if (src == NULL)
		return false;
	const char *first = src;
	double value;
	if (!ParseDouble(first, src + strlen(src), value))
		return false;
	dest = static_cast<float>(value);
	return true;
}


template<unsigned int B> //B = base/radix
bool ConvertIntToStr(int src, char *dest)
{
	if (src < 0)
		*dest++ = '-';
	const unsigned int magnitude = src < 0 ? 0u - static_cast<unsigned int>(src) : static_cast<unsigned int>(src);
	dest = WriteUnsigned<B>(dest, magnitude);
	*dest = 0;
	return true;
}
//instantiate templates
template bool ConvertIntToStr<16>(int src, char *dest);
template bool ConvertIntToStr<10>(int src, char *dest);
template bool ConvertIntToStr<2>(int src, char *dest);

bool ConvertIntToStr(int src, char *dest)
{
	if (dest == NULL)
		return false;
	return ConvertIntToStr<10>(src, dest);
}

// karma real_inserter<double, real_policies<double>>: precision 3, trailing
// zeros trimmed, scientific notation outside [1e-3, 1e5).
bool ConvertFloatToStr(float src, char *dest)
{
	if (dest == NULL)
		return false;

	double n = src;
	if (std::isnan(n) || std::isinf(n))
	{
		if (std::signbit(n))
			*dest++ = '-';
		std::strcpy(dest, std::isnan(n) ? "nan" : "inf");
		return true;
	}

	const bool scientific = n != 0 && (std::fabs(n) >= 1e5 || std::fabs(n) < 1e-3);
	bool sign_val = false;
	if (std::signbit(n))
	{
		n = -n;
		sign_val = true;
	}

	const unsigned precision = 3;
	double dim = 0;
	if (scientific && n != 0)
	{
		dim = std::log10(n);
		if (dim > 0)
			n /= Pow10(static_cast<int>(static_cast<long>(dim)));
		else if (n < 1.)
		{
			long exp = static_cast<long>(-dim);
			if (exp != -dim)
				++exp;
			dim = static_cast<double>(-exp);
			if (exp > MaxExponent10)
			{
				n *= Pow10(MaxExponent10);
				n *= Pow10(static_cast<int>(exp) - MaxExponent10);
			}
			else
				n *= Pow10(static_cast<int>(exp));
		}
	}

	double integer_part;
	const double precexp = Pow10(precision);
	double fractional_part = std::modf(n, &integer_part);
	fractional_part = std::floor(fractional_part * precexp + 0.5);
	if (fractional_part >= precexp)
	{
		fractional_part = std::floor(fractional_part - precexp);
		integer_part += 1; // rounding overflow
	}

	const double long_int_part = std::floor(integer_part);
	double long_frac_part = fractional_part;
	unsigned prec = precision;
	{
		const double frac_part_floor = long_frac_part;
		if (long_frac_part != 0)
		{
			while (prec != 0 && static_cast<long>(std::fmod(long_frac_part, 10.0)) == 0)
			{
				long_frac_part = long_frac_part / 10;
				--prec;
			}
		}
		else
			prec = 0;
		if (precision != prec)
			long_frac_part = frac_part_floor / Pow10(precision - prec);
	}

	if (sign_val && long_int_part == 0 && long_frac_part == 0)
		sign_val = false; // zero result, no sign

	if (sign_val)
		*dest++ = '-';
	dest = WriteIntegral(dest, long_int_part);
	*dest++ = '.';
	const int digits = static_cast<int>((long_frac_part == 0 ? 0 : std::floor(std::log10(long_frac_part))) + 1);
	for (int d = digits; d < static_cast<int>(prec); ++d)
		*dest++ = '0';
	dest = WriteIntegral(dest, long_frac_part);

	if (scientific)
	{
		const long exponent = static_cast<long>(dim);
		*dest++ = 'e';
		if (exponent < 0)
			*dest++ = '-';
		const long abs_exp = exponent < 0 ? -exponent : exponent;
		if (abs_exp < 10)
			*dest++ = '0';
		char buf[24];
		int count = 0;
		long v = abs_exp;
		do { buf[count++] = static_cast<char>('0' + v % 10); v /= 10; } while (v != 0);
		while (count > 0)
			*dest++ = buf[--count];
	}
	*dest = 0;
	return true;
}


void amx_SetCString(AMX* amx, cell param, const char *str, int len)
{
	cell *dest = NULL;
	amx_GetAddr(amx, param, &dest);
	amx_SetString(dest, str, 0, 0, (len > 0) ? len : (strlen(str)+1));
}
