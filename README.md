# Objeck
Objeck is an object-oriented computer language with functional features. The language has ties with Java, Scheme and UML. In this language all data types, except for higher-order functions, are treated as objects.

![alt text](images/design2.png "Compiler & VM")

Objeck is a general-purpose programming language with an emphasis placed on simplicity. The programming environment consists of a compiler, virtual machine and command line debugger.

```ruby
class Hello {
  function : Main(args : String[]) ~ Nil {
    "Hello World" → PrintLine();
    "Καλημέρα κόσμε" → PrintLine();
    "こんにちは 世界" → PrintLine();
  }
}
```

See more on [Rosetta Code](http://rosettacode.org/wiki/Category:Objeck) and checkout the following [programming tasks](programs/rc).

Notable features:

* Object-oriented and functional
  * Classes, interfaces and higher-order functions
  * Anonymous classes 
  * Reflection 
  * Object serialization 
  * Type inference
* Native support for threads, sockets, files, date/time, etc.
* Libraries 
  *  Collections (vectors, queues, trees, hashes, etc.)
  *  HTTP and HTTPS clients
  *  RegEx
  *  JSON and XML parsers
  *  Encryption
  *  Database access
  *  Data structure querying
  *  2D Gaming
* Garbage collection
* JIT support (IA-32 and AMD64)

## Documentation
Please refer to the programmer's guide [documentation](http://www.objeck.org/documentation/) and [online tutorial](http://www.objeck.org/tutorial/). Also checkout [Rosetta Code](http://rosettacode.org/wiki/Category:Objeck) [examples](programs/rc).

## Deployment

Build and deployment [instructions](http://www.objeck.org/developers/) for Windows, Linux and OS X. 

## Binaries
Get the latest [binaries](https://sourceforge.net/projects/objeck-lang/).
