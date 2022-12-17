/* Compute the gcd of 1071, 1029:  21 */
 
a = 1071;
b = 1029;
 
while (b != 0) {
    new_a = b;
    b     = a % b;
    a     = new_a;
}
print(a);