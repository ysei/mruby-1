 p 2**64 + 1
 a = Bignum.new 
 a = a.int_to_big 2**16 + 1
 #a = a + 2**16 + 1
 p a.to_s
 a = a + 112358000000000000000
 p a.to_s
 b = Bignum.new 
 b = b.int_to_big 0
 p b.to_s
 b = b + 4*1024*1024 + 1024
 b = b + a
 b = b + b
 p b.to_s