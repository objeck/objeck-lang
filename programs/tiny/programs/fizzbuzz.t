/* FizzBuzz */
i = 1;
while (i <= 100) {
    if (!(i % 15))
        print("FizzBuzz");
    else if (!(i % 3))
        print("Fizz");
    else if (!(i % 5))
        print("Buzz");
    else
        print(i);
 
    print("\n");
    i = i + 1;
}