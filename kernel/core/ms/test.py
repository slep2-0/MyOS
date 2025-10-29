a = int(input("Enter the first number > "))
b = int(input("Enter the second number > "))
c = int(input("Enter the third number > "))

temp = 0

if a > b:
    temp = a
    a = b
    b = temp

if a > c:
    temp = a
    a = c
    c = temp

if b > c:
    temp = b
    b = c
    c = temp

print(a, b, c)