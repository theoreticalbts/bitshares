
#include <iostream>
#include <string>

#include <fc/optional.hpp>
#include <fc/variant.hpp>

#include <fc/exception/exception.hpp>

#include <fc/io/json_relaxed.hpp>
#include <fc/io/json.hpp>

#define VARIANT        1
#define EXCEPTION      2
#define SAME_AS_PREV   3
#define UNKNOWN        4

bool compare_variant( const fc::variant& a, const fc::variant& b )
{
    fc::variant::type_id ta = a.get_type(), tb = b.get_type();
    
    switch( ta )
    {
        case fc::variant::null_type:
            return (tb == fc::variant::null_type);
            
        case fc::variant::int64_type:
            switch( tb )
            {
                case fc::variant::int64_type:
                    return a.as_int64() == b.as_int64();
                case fc::variant::uint64_type:
                {
                    int64_t i64_a = a.as_int64();
                    if( i64_a < 0 )
                        return false;
                    return static_cast<uint64_t>(i64_a) == b.as_uint64();
                }
                default:
                    return false;
            }
            break;
            
        case fc::variant::uint64_type:
            switch( tb )
            {
                case fc::variant::int64_type:
                {
                    int64_t i64_b = b.as_int64();
                    if( i64_b < 0 )
                        return false;
                    return a.as_uint64() == static_cast<uint64_t>(i64_b);
                }
                case fc::variant::uint64_type:
                    return a.as_uint64() == b.as_uint64();
                default:
                    return false;
            }
            
        case fc::variant::double_type:
            if( tb != fc::variant::double_type )
                return false;
            return a.as_double() == b.as_double();
            
        case fc::variant::bool_type:
            if( tb != fc::variant::bool_type )
                return false;
            return a.as_bool() == b.as_bool();
            
        case fc::variant::string_type:
            if( tb != fc::variant::string_type )
                return false;
            return a.as_string() == b.as_string();
            
        case fc::variant::blob_type:
            FC_ASSERT(false, "blobs not supported by this test framework");

        case fc::variant::array_type:
        {
            if( tb != fc::variant::array_type )
                return false;
        
            const fc::variants& aa = a.get_array();
            const fc::variants& ab = b.get_array();
            
            size_t n = aa.size();
            if( ab.size() != n )
                return false;
            
            for( size_t i=0; i<n; i++ )
            {
                if( !compare_variant(aa[i], ab[i]) )
                    return false;
            }
            
            return true;
        }
            
        case fc::variant::object_type:
        {
            // n.b. this relies on order preserving objects
            if( tb != fc::variant::object_type )
                return false;
                
            const fc::variant_object& oa = a.get_object();
            const fc::variant_object& ob = b.get_object();
            
            size_t n = oa.size();
            if( ob.size() != n )
                return false;

            fc::variant_object::iterator ia = oa.begin();
            fc::variant_object::iterator ib = ob.begin();

            while( true )
            {
                if( ia == oa.end() )
                {
                    FC_ASSERT( ib == ob.end() );
                    break;
                }
                
                const fc::variant_object::entry ea = *ia, eb = *ib;

                if( ea.key() != eb.key() )
                    return false;
                if( !compare_variant(ea.value(), eb.value()) )
                    return false;

                ia++;
                ib++;
            }
            return true;
        }

        default:
            FC_ASSERT( false, "unknown type in compare_variant" );
    }
}

class test_output;

class test_output
{
public:
    template<typename T>
    test_output( const T& v )
    {
        this->output_type = VARIANT;
        this->output_value = v;
        return;
    }
    
    test_output()
    {
        this->output_type = SAME_AS_PREV;
        return;
    }

    static test_output create( int output_type=SAME_AS_PREV )
    {
        test_output result = test_output();
        result.output_type = output_type;
        return result;
    }

    std::string str() const
    {
        switch( this->output_type )
        {
            case VARIANT:
                return fc::json::to_string( this->output_value );
            case EXCEPTION:
                return "EXCEPTION";
            case SAME_AS_PREV:
                return "SAME_AS_PREV";
            case UNKNOWN:
                return "UNKNOWN";
            default:
                FC_ASSERT( false, "unknown output_type" );
        }
    }
    
    void check( const char* parser_name, const char* test_name, const test_output& expected, test_output& expected_prev ) const
    {
        fc::variant v_expected;
        test_output actually_expected = expected;
        
        if( expected.output_type == SAME_AS_PREV )
        {
            FC_ASSERT( expected_prev.output_type != UNKNOWN, "SAME_AS_PREV specified, but prev is UNKNOWN (check test definition)" );
            actually_expected = expected_prev;
        }

        bool ok;
        switch( actually_expected.output_type )
        {
            case VARIANT:
                ok = ((this->output_type == VARIANT) && compare_variant(this->output_value, actually_expected.output_value));
                break;
            case EXCEPTION:
                ok = (this->output_type == EXCEPTION);
                break;
            case SAME_AS_PREV:
                FC_ASSERT( false, "SAME_AS_PREV should have been resolved to an actual type, but wasn't" );
            case UNKNOWN:
                FC_ASSERT( false, "SAME_AS_PREV specified, but prev is UNKNOWN (check test definition)" );
            default:
                FC_ASSERT( false, "unknown output_type" );
        }
        
        if( !ok )
        {
            std::cout << parser_name << " parser failed test " << test_name << "\n"
                         "    expected : " << actually_expected.str() << "\n"
                         "    got      : " <<    this->str() << "\n";
        }
        else
            std::cout << parser_name << " parser passed test " << test_name << "\n";

        expected_prev = actually_expected;
        return;
    }

    int output_type;
    fc::variant output_value;
};

test_output run_test(
    const char* test_name,
    const char* input_data,
    const test_output& expected,
          test_output& expected_prev,
    fc::json::parse_type ptype
    )
{
    const char* parser_name;
    
    switch( ptype )
    {
        case fc::json::legacy_parser:
            parser_name = "legacy";
            break;
        case fc::json::strict_parser:
            parser_name = "strict";
            break;
        case fc::json::relaxed_parser:
            parser_name = "relaxed";
            break;
        default:
            FC_ASSERT( false, "unknown parser ID" );
    }

    std::cout << parser_name << " parser running on test " << test_name << "\n";

    test_output result;

    try
    {
        result.output_value = fc::json::from_string( input_data, ptype );
        result.output_type = VARIANT;
    }
    catch( const fc::parse_error_exception& e )
    {
        result.output_type = EXCEPTION;
    }
    catch( const fc::eof_exception& e )
    {
        result.output_type = EXCEPTION;
    }
    catch( const fc::exception& e )
    {
        std::cout << e.to_string() << "\n";
        throw(e);
    }

    result.check( parser_name, test_name, expected, expected_prev );
    return result;
}

class test_runner
{
public:
    test_runner() {}
    
    test_runner& operator() (
        const char* test_name,
        const char* input_data,
        test_output expected_output_relaxed = test_output::create(EXCEPTION),
        test_output expected_output_strict = test_output::create(SAME_AS_PREV),
        test_output expected_output_legacy = test_output::create(SAME_AS_PREV)
        )
    {
        test_output expected_prev = test_output::create( UNKNOWN );
        run_test( test_name, input_data, expected_output_relaxed, expected_prev, fc::json::relaxed_parser );
        run_test( test_name, input_data, expected_output_strict , expected_prev, fc::json::strict_parser  );
        run_test( test_name, input_data, expected_output_legacy , expected_prev, fc::json::legacy_parser  );
        return (*this);
    }
};

int main( int argc, char** argv, char** envp )
{
    fc::variant v_null;
    fc::variant v_true(true);
    fc::variant v_false(false);
    test_output exc = test_output::create(EXCEPTION);

    test_runner runner;

    // Each case has the following parameters:
    //   test name
    //   input (JSON formatted)
    //   expected output from relaxed parser
    //   expected output from strict parser
    //   expected output from legacy parser
    
    fc::variant big_number = fc::variant( 9223372036854775806ull );
    
    runner
        ("empty_string",             "", exc)
        ("letter_a", "a",            "a", exc)
        ("letter_t", "t",            "t", exc, "t")
        ("letter_f", "f",            "f", exc, "f")
        ("letter_n", "n",            "n", exc, "n")
        ("almost_true", "tru",       "tru", exc, "tru")
        ("almost_false", "fals",     "fals", exc, "fals")
        ("almost_null", "nul",       "nul", exc, "nul")
        ("python_true", "True",      "True", exc)
        ("python_false", "False",    "False", exc)
        ("titlecase_null", "Null",   "Null", exc)
        ("python_none", "None",      "None", exc)
        ("lowercase_none", "none",   "none", exc, "none")
        ("bool_true", "true",        v_true)
        ("bool_false", "false",      v_false)
        ("actual_null", "null",      v_null)

        // compliant integers
        ("digit_0", "0",             0)
        ("digit_1", "1",             1)
        ("digit_2", "2",             2)
        ("digit_3", "3",             3)
        ("digit_4", "4",             4)
        ("digit_5", "5",             5)
        ("digit_6", "6",             6)
        ("digit_7", "7",             7)
        ("digit_8", "8",             8)
        ("digit_9", "9",             9)
        ("number_10", "10",          10)
        ("number_12389", "12389",    12389)
        ("uint_dec_7fff:ffff:ffff:fffe", "9223372036854775806", 9223372036854775806ull)
        ("uint_dec_7fff:ffff:ffff:ffff", "9223372036854775807", 9223372036854775807ull)
        ("uint_dec_8000:0000:0000:0000", "9223372036854775808", 9223372036854775808ull)
        ("uint_dec_8000:0000:0000:0001", "9223372036854775809", 9223372036854775809ull)
        ("uint_dec_ffff:ffff:ffff:fffe", "18446744073709551614", 18446744073709551614ull)
        ("uint_dec_ffff:ffff:ffff:ffff", "18446744073709551615", 18446744073709551615ull)
        ("uint_dec_1:0000:0000:0000:0000", "18446744073709551616", exc)
        ("uint_dec_20000000000000000000", "20000000000000000000", exc)
        ("int_dec_-1", "-1",         -1)
        ("int_dec_-10", "-10",       -10)
        ("int_dec_8000:0000:0000:0001", "-9223372036854775807", -9223372036854775807ll)
        ("int_dec_8000:0000:0000:0000", "-9223372036854775808", -9223372036854775808ll)
        ("int_dec_7fff:ffff:ffff:ffff:ov", "-9223372036854775809", exc)
        ("int_-0", "-0",             0)

        // non-compliant integers
        ("uint_dec_+0", "+0",        0)
        ("uint_dec_+100", "+100",    100)


        // non-base-10 integers
        ("bin_0b100", "0b100",       4, exc)
        ("bin_0B1010001101", "0B1010001101", 653, exc)
        ("bin_-0B1010001101", "-0B1010001101", -653, exc)
        ("oct_0o775", "0o775",       509, exc)
        ("oct_0O77", "0O77",         63, exc)
        ("hex_cafebabe", "0xcafebabe", 0xcafebabe, exc)
        ("hex_cef", "0xcef",         0xcef, exc)
        ("hex_07bc3de2", "0x7bc3de2", 0x7bc3de2, exc)
        
        // compliant FP
        ("fp_0.5", "0.5",            0.5)
        ("fp_0.25", "0.25",          0.25)
        ("fp_0.125", "0.125",        0.125)
        ("fp_0.28374481201171875", "0.28374481201171875", 0.28374481201171875)
        ("fp_3.0e3", "3.0e3",        3.0e3)
        ("fp_3.25e55", "3.25e55",    3.25e55)
        ("fp_11.125E104", "11.125E104", 11.125e104)
        ("fp_7e8", "7e8",            7e8)
        ("fp_99e+17", "99e+17",      99e+17)
        ("fp_713e-70", "713e-70",    713e-70)
        ("fp_12.1875E-5", "12.1875E-5", 12.1875E-5)
        ("fp_-3.0", "-3.0",          -3.0)
        ("fp_-3.0E-3", "-3.0E-3",    -3.0E-3)
        ("fp_0e0", "0e0",            0e0)

        // non-compliant FP
        ("fp_.5", ".5",              0.5, exc, 0.5)
        ("fp_.125", ".125",          0.125, exc, 0.125)
        ("fp_.", ".",                ".", exc)
        ("fp_4.3.5", "4.3.5",        "4.3.5", exc)
        ("fp_192.168.1.1", "192.168.1.1", "192.168.1.1", exc)
        ("fp_2.", "2.",              2.0, exc, 2.0)
        ("fp_-314159.", "-314159.",  -314159.0, exc, -314159.0)
        ("fp_77.e9", "77.e9",        77.0e9, exc, 77.0e9)
        ("fp_.e", ".e",              ".e", exc)
        ("fp_719.3z", "719.3z",      "719.3z", exc)
        ("fp_0e", "0e",              "0e", exc)

        ("empty_array", "[]",        fc::variants())
        ("empty_object", "{}",       fc::variant_object())
        ;
    return 0;
}
