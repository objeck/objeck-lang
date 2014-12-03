## Objeck
Objeck is an object-oriented computer language with functional features. The language has ties with Java, Scheme and UML. In this language all data types, except for higher-order functions, are treated as objects.

The language contains all of the features of a general-purpose computing language with an emphasis placed on simplicity. The programming environment consists of a compiler, virtual machine and command line debugger.

### Downloads

Get the [latest release]{2https://sourceforge.net/projects/objeck-lang/}.

### Simple example
```objeck
class Hello {
  function : Main(args : String[]) ~ Nil {
    "Hello World"->PrintLine();
    "Καλημέρα κόσμε"->PrintLine();
    "こんにちは 世界"->PrintLine();
  }
}
```

Compiling: ```obc -src hello.obs -dest hello.obe```

Running: ```obr hello.obe```

### Documentation
Please refer to the project website for [documentation](http://www.objeck.org/documentation/) and [tutorials](http://www.objeck.org/tutorial/).

### Pulling the code
```git clone https://github.com/objeck/objeck-lang.git objeck-lang```

### Building your own
[Build instructions](http://www.objeck.org/developers/) for Windows, Linux and OS X. 


