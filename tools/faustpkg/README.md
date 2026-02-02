# faustpkg

Simple package installer for Faust packages stored on GitHub.

## Package layout (GitHub repo)

```
Faust.toml
package.dsp
src/
  test_package.lib
tests/
  demo.dsp
```

`Faust.toml` must contain at least:

```
name = "test_package"
version = "1.0"
entry = "src/test_package.lib"
```

## Build

```
cmake -S tools/faustpkg -B build/faustpkg
cmake --build build/faustpkg
```

## Usage

Install from `https://github.com/Mo-Khater/<name>` (tag `v<version>`):

```
faustpkg install test_package==1.0
```

Override repo:

```
faustpkg install test_package==1.0 --repo owner/repo
```

Cache root (default `e:/gsoc-faust/faust/faust-packages`):

```
faustpkg install test_package==1.0 --cache e:/gsoc-faust/faust/faust-packages
```

## Cache layout

```
faust-packages/
  test_package/
    1.0/
      Faust.toml
      package.dsp
      src/
      tests/
    package.dsp        # current version (copied)
```

If `package.dsp` is missing, `faustpkg` will generate it as a thin wrapper:

```
import("src/test_package.lib");
```

The compiler resolves `package("test_package")` to:

```
e:/gsoc-faust/faust/faust-packages/test_package/package.dsp
```
