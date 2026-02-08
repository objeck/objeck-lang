# jedit-objeck-mode
Objeck mode for JEdit

This is still in alpha state.

I'm using JEdit on Windows, data directory is `%appdata%\jEdit`. Put `objeck.xml` on `%appdata%\jEdit\modes` and edit/create if not exists the file named `catalog`:

```
<?xml version="1.0"?>
<!DOCTYPE MODES SYSTEM "catalog.dtd">

<MODES>

<!-- Add lines like the following, one for each edit mode you add: -->
<!-- <MODE NAME="foo" FILE="foo.xml" FILE_NAME_GLOB="*.foo" /> -->

<MODE NAME="objeck" FILE="objeck.xml" FILE_NAME_GLOB="*.obs" />

</MODES>
```
