# Objeck Language Regression Test Suite

## Purpose

This regression test suite validates critical functionality after major changes to the Objeck language compiler and virtual machine. It focuses on:

- **Crypto operations** - Validates mbedTLS migration (SHA hashes, AES-256, Base64)
- **ARM64 JIT fixes** - Tests ARM64-specific code generation issues
- **Core language features** - Ensures fundamental language semantics remain correct
- **Cross-platform consistency** - Verifies x64 and ARM64 produce identical results

## Running Tests

### Linux/macOS

```bash
cd programs/regression
chmod +x run_regression.sh
./run_regression.sh x64
```

### Windows

```cmd
cd programs\regression
run_regression.cmd x64      REM For x64 build
run_regression.cmd arm64    REM For ARM64 build
```

## Test Organization

Tests are organized by category with descriptive names:

### Category A: Crypto Operations
- `crypto_comprehensive.obs` - Comprehensive mbedTLS migration tests (hash, AES, Base64)

### Category B: ARM64 JIT Fixes
- `arm64_char_arrays.obs` - Character array operations (STRH/LDRH encoding)
- `arm64_large_immediates.obs` - Arithmetic with immediates > 4095
- `arm64_bitwise_not.obs` - Bitwise NOT operation (64-bit ORN)
- `arm64_multiply_constants.obs` - Multiply-by-constant optimization

### Category C: Core Language Features
- `core_arithmetic.obs` - Basic arithmetic, type conversions, operator precedence
- `core_classes.obs` - Class instantiation, inheritance, method calls
- `core_collections.obs` - Vector, Map, generics, lambdas

## Adding New Tests

1. Create a new `.obs` file in this directory
2. Name it descriptively: `category_feature.obs`
3. Include a comment header explaining what it tests
4. Output `PASS` or `FAIL` messages for automation
5. Exit with code 0 on success, non-zero on failure

### Test Template

```objeck
#~
# Test Name
# Brief description of what this tests
# Related commit: <commit-hash>
~#

class MyTest {
    function : Main(args : String[]) ~ Nil {
        "Testing my feature..."->PrintLine();

        pass := true;

        # Your test logic here
        if(test_condition) {
            # Test passed
        } else {
            pass := false;
            "  FAIL: Description of failure"->PrintLine();
        };

        if(pass) {
            "PASS: My feature"->PrintLine();
        } else {
            "FAIL: My feature"->PrintLine();
        };
    }
}
```

## CI/CD Integration

This test suite runs automatically in GitHub Actions on:
- Push to master
- Pull requests to master

See `.github/workflows/c-cpp.yml` for CI configuration.

## Expected Output

```
========================================
  Objeck Regression Test Suite
  Platform: x64
========================================

Running: crypto_comprehensive...
  ✅ PASS
Running: arm64_char_arrays...
  ✅ PASS
Running: core_arithmetic...
  ✅ PASS

========================================
  Results: 7 passed, 0 failed
========================================
```

## Cross-Platform Testing

To verify x64 and ARM64 produce identical results:

```bash
# Run on both platforms
./run_regression.sh x64
mv results results_x64

./run_regression.cmd arm64   # Windows ARM64
mv results results_arm64

# Compare outputs
./compare_platforms.sh
```

## Related Documentation

- [Main Testing Documentation](../../core/compiler/regress.sh)
- [GitHub Actions Workflow](../../.github/workflows/c-cpp.yml)
- [ARM64 Build Fix](../../core/lib/crypto/WINDOWS_ARM64_BUILD_FIX.md)

## Maintenance

- Run regression tests before major releases
- Add tests for new features and bug fixes
- Update test expectations if language semantics change intentionally
- Keep test suite small (< 30 tests) for fast CI execution
