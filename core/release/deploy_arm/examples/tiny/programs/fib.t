/* fibonacci of 44 is 701408733 */
 
n = 44;
i = 1;
a = 0;
b = 1;
while (i < n) {
    w = a + b;
    a = b;
    b = w;
    i = i + 1;
}
print(w, "\n");