 a = Bignum.new 
 b = Bignum.new 
 c = Bignum.new 
 a = a.int_to_big 7000000
 p a.to_s
 b = b.int_to_big 3000000
 p b.to_s
 c = a * b
 p c.to_s
 c = c / a
 p c.to_s
 c = a ** 3
 p c.to_s

 p 2**64 + 1
 a = Bignum.new 
 a = a.int_to_big 2**16 + 1
 #a = a + 2**16 + 1
 p a.to_s
 a = a + 112358000000000000000
 p a.to_s
 a = a.int_to_big 0
 a = a + 112358000000000000000
 p a.to_s
 b = Bignum.new 
 b = b.int_to_big 0
 p b.to_s
 #b = b + 4*1024*1024 + 1024
 b = b + a
 p b.to_s
 #b = b + 10*1024
 b = a ** 5
 p b.to_s
 b1789 = b.int_to_big 1789
 b = a * b1789
 p "a = "+a.to_s
 p "b = "+b.to_s
 c = b % a
 p c.to_s
 c = b / a
 p c.to_s
 b = a * 1000 + b1789
 b = b % a
 p b.to_s
 

