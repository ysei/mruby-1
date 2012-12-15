 a = Bignum.new 
 a = a.int_to_big 50
 p a.to_s
 a = a + 112358000000000000000
 p a.to_s
 b = Bignum.new 
 b = b.int_to_big 0
 p b.to_s
 b = b + 1
 p b.to_s