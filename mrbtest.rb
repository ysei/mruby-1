$ok_test = 0
$ko_test = 0
$kill_test = 0
$asserts  = []
$test_start = Time.now if Object.const_defined?(:Time)

##
# Print the assertion in a readable way
def print_assertion_string(str, iso)
  print(str)
  if(iso != '')
    print(' [')
    print(iso)
    print(']')
  end
end

##
# Verify a code block.
#
# str : A remark which will be printed in case
#       this assertion fails
# iso : The ISO reference code of the feature
#       which will be tested by this
#       assertion
def assert(str = 'Assertion failed', iso = '')
  begin
    if(!yield)
      $asserts.push(['Fail: ', str, iso])
      $ko_test += 1
      print('F')
    else
      $ok_test += 1
      print('.')
    end
  rescue Exception => e
    $asserts.push(['Error: ', str, iso, e])
    $kill_test += 1
    print('X')
  end
end

##
# Report the test result and print all assertions
# which were reported broken.
def report()
  print "\n"
  $asserts.each do |err, str, iso, e|
    print(err);
    print_assertion_string(str, iso)
    if e
      print(" => ")
      print(e.message)
    end
    print("\n")
  end

  $total_test = $ok_test.+($ko_test)
  print('Total: ')
  print($total_test)
  print("\n")

  print('   OK: ')
  print($ok_test)
  print("\n")
  print('   KO: ')
  print($ko_test)
  print("\n")
  print('Crash: ')
  print($kill_test)
  print("\n")

  if Object.const_defined?(:Time)
    print(' Time: ')
    print(Time.now - $test_start)
    print(" seconds\n")
  end
end

##
# Performs fuzzy check for equality on methods returning floats
# on the basis of the Math::TOLERANCE constant.
def check_float(a, b)
  tolerance = Math::TOLERANCE
  a = a.to_f
  b = b.to_f
  if a.finite? and b.finite?
    (a-b).abs < tolerance
  else
    true
  end
end
##
# ArgumentError ISO Test

assert('ArgumentError', '15.2.24') do
  e2 = nil
  a = []
  begin
    # this will cause an exception due to the wrong arguments
    a[]
  rescue => e1
    e2 = e1
  end

  ArgumentError.class == Class and e2.class == ArgumentError
end

assert('ArgumentError superclass', '15.2.24.2') do
  ArgumentError.superclass == StandardError
end

##
# Array ISO Test

assert('Array', '15.2.12') do
  Array.class == Class
end

assert('Array superclass', '15.2.12.2') do
  Array.superclass == Object
end

assert('Array.[]', '15.2.12.4.1') do
  Array.[](1,2,3) == [1, 2, 3]
end

assert('Array#*', '15.2.12.5.1') do
  [1].*(3) == [1, 1, 1]
end

assert('Array#+', '15.2.12.5.2') do
  [1].+([1]) == [1, 1]
end

assert('Array#<<', '15.2.12.5.3') do
  [1].<<(1) == [1, 1]
end

assert('Array#[]', '15.2.12.5.4') do
  e2 = nil
  e3 = nil
  a = Array.new
  begin
    # this will cause an exception due to the wrong arguments
    a.[]()
  rescue => e1
    e2 = e1
  end
  begin
    # this will cause an exception due to the wrong arguments
    a.[](1,2,3)
  rescue => e1
    e3 = e1
  end

  [1,2,3].[](1) == 2 and
    e2.class == ArgumentError and
    e3.class == ArgumentError
end

assert('Array#[]=', '15.2.12.5.5') do
  e2 = nil
  e3 = nil
  a = Array.new
  begin
    # this will cause an exception due to the wrong arguments
    a.[]=()
  rescue => e1
    e2 = e1
  end
  begin
    # this will cause an exception due to the wrong arguments
    a.[]=(1,2,3,4)
  rescue => e1
    e3 = e1
  end

  [1,2,3].[]=(1,4) == [1, 4, 3] and
  [1,2,3].[]=(1,2,3) == [1, 3] and
    e2.class == ArgumentError and
    e3.class == ArgumentError
end

assert('Array#clear', '15.2.12.5.6') do
  a = [1]
  a.clear
  a == []
end

assert('Array#collect!', '15.2.12.5.7') do
  a = [1,2,3]
  a.collect! { |i| i + i }
  a == [2,4,6]
end

assert('Array#concat', '15.2.12.5.8') do
  a = [1,2]
  b = [3,4]
  a.concat(b) == [1,2,3,4]
end

assert('Array#delete_at', '15.2.12.5.9') do
  a = [1,2,3]
  a.delete_at(1)
  a == [1,3]
end

assert('Array#each', '15.2.12.5.10') do
  a = [1,2,3]
  b = 0
  a.each {|i| b += i}
  b == 6
end

assert('Array#each_index', '15.2.12.5.11') do
  a = [1]
  b = nil
  a.each_index {|i| b = i}
  b == 0
end

assert('Array#empty?', '15.2.12.5.12') do
  a = []
  b = [b]
  a.empty? and not b.empty?
end

assert('Array#first', '15.2.12.5.13') do
  a = []
  b = [1,2,3]

  e2 = nil
  e3 = nil
  begin
    # this will cause an exception due to the wrong argument
    [1,2,3].first(-1)
  rescue => e1
    e2 = e1
  end
  begin
    # this will cause an exception due to the wrong argument
    [1,2,3].first(1,2)
  rescue => e1
    e3 = e1
  end

  a.first == nil and b.first == 1 and b.first(0) == [] and
    b.first(1) == [1] and b.first(4) == [1,2,3] and
    e2.class == ArgumentError and e3.class == ArgumentError
end

assert('Array#index', '15.2.12.5.14') do
  a = [1,2,3]

  a.index(2) == 1
end

assert('Array#initialize', '15.2.12.5.15') do
  a = [].initialize(1)
  b = [].initialize(2)
  c = [].initialize(2, 1)
  d = [].initialize(2) {|i| i}

  a == [nil] and b == [nil,nil] and c == [1,1] and d == [0,1]
end

assert('Array#initialize_copy', '15.2.12.5.16') do
  a = [1,2,3]
  b = [].initialize_copy(a)

  b == [1,2,3]
end

assert('Array#join', '15.2.12.5.17') do
  a = [1,2,3].join
  b = [1,2,3].join(',')

  a == '123' and b == '1,2,3'
end

assert('Array#last', '15.2.12.5.18') do
  a = [1,2,3]

  e2 = nil
  begin
    # this will cause an exception due to the wrong argument
    [1,2,3].last(-1)
  rescue => e1
    e2 = e1
  end

  a.last == 3 and [].last == nil and e2.class == ArgumentError
end

assert('Array#length', '15.2.12.5.19') do
  a = [1,2,3]

  a.length == 3
end

assert('Array#map!', '15.2.12.5.20') do
  a = [1,2,3]
  a.map! { |i| i + i }
  a == [2,4,6]
end

assert('Array#pop', '15.2.12.5.21') do
  a = [1,2,3]
  b = a.pop

  [].pop == nil and a == [1,2] and b = 3
end

assert('Array#push', '15.2.12.5.22') do
  a = [1,2,3]
  b = a.push(4)

  a == [1,2,3,4] and b = [1,2,3,4]
end

assert('Array#replace', '15.2.12.5.23') do
  a = [1,2,3]
  b = [].replace(a)

  b == [1,2,3]
end

assert('Array#reverse', '15.2.12.5.24') do
  a = [1,2,3]
  b = a.reverse

  a == [1,2,3] and b == [3,2,1]
end

assert('Array#reverse!', '15.2.12.5.25') do
  a = [1,2,3]
  b = a.reverse!

  a == [3,2,1] and b == [3,2,1]
end

assert('Array#rindex', '15.2.12.5.26') do
  a = [1,2,3]

  a.rindex(2) == 1
end

assert('Array#shift', '15.2.12.5.27') do
  a = [1,2,3]
  b = a.shift

  [].shift == nil and a == [2,3] and b == 1
end

assert('Array#size', '15.2.12.5.28') do
  a = [1,2,3]

  a.size == 3
end

assert('Array#slice', '15.2.12.5.29') do
  a = "12345".slice(1, 3)
  b = a.slice(0)
  "#{b}:" == "2:" and [1,2,3].[](1) == 2
end

assert('Array#unshift', '15.2.12.5.30') do
  a = [2,3]
  b = a.unshift(1)

  a == [1,2,3] and b == [1,2,3]
end

assert('Array#to_s', '15.2.12.5.31') do
  a = [2, 3,   4, 5]
  r1 = a.to_s
  r2 = a.inspect

  r1 == r2 and r1 == "[2, 3, 4, 5]"
end

assert('Array#==', '15.2.12.5.33') do
  r1 = [ "a", "c" ]    == [ "a", "c", 7 ]     #=> false
  r2 = [ "a", "c", 7 ] == [ "a", "c", 7 ]     #=> true
  r3 = [ "a", "c", 7 ] == [ "a", "d", "f" ]   #=> false

  r1 == false and r2 == true and r3 == false
end

assert('Array#<=>', '15.2.12.5.36') do
  r1 = [ "a", "a", "c" ]    <=> [ "a", "b", "c" ]   #=> -1
  r2 = [ 1, 2, 3, 4, 5, 6 ] <=> [ 1, 2 ]            #=> +1

  r1 == -1 and r2 == +1
end

# Not ISO specified

assert("Array (Shared Array Corruption)") do
  a = [ "a", "b", "c", "d", "e", "f" ]
  b = a.slice(1, 3)
  a.clear
  b.clear
end

##
# BasicObject

assert('BasicObject') do
  BasicObject.class == Class
end

assert('BasicObject superclass') do
  BasicObject.superclass == nil
end

##
# Bootstrap tests for blocks

assert('BS Block 1') do
  1.times{
    begin
      a = 1
    ensure
      foo = nil
    end
  } == 1
end

assert('BS Block 2') do
  [1,2,3].find{|x| x == 2} == 2
end

assert('BS Block 3') do
  class E
    include Enumerable
    def each(&block)
      [1, 2, 3].each(&block)
    end
  end
  E.new.find {|x| x == 2 } == 2
end

assert('BS Block 3') do
  sum = 0
  for x in [1, 2, 3]
    sum += x
  end
  sum == 6
end

assert('BS Block 4') do
  sum = 0
  for x in (1..5)
    sum += x
  end
  sum == 15
end

assert('BS Block 5') do
  sum = 0
  for x in []
    sum += x
  end
  sum == 0
end

assert('BS Block 6') do
  ans = []
  1.times{
    for n in 1..3
      a = n
      ans << a
    end
  } == 1
end

assert('BS Block 7') do
  ans = []
  for m in 1..3
    for n in 2..4
      a = [m, n]
      ans << a
    end
  end == (1..3)
end

assert('BS Block 8') do
  (1..3).to_a == [1, 2, 3]
end

assert('BS Block 9') do
  (1..3).map{|e|
    e * 4
  } == [4, 8, 12]
end

assert('BS Block 10') do
  def m
    yield
  end
  def n
    yield
  end

  m{
    n{
      100
    }
  } == 100
end

assert('BS Block 11') do
  def m
    yield 1
  end

  m{|ib|
    m{|jb|
      i = 20
    }
  } == 20
end

assert('BS Block 12') do
  def m
    yield 1
  end

  m{|ib|
    m{|jb|
      ib = 20
      kb = 2
    }
  } == 2
end

assert('BS Block 13') do
  def iter1
    iter2{
      yield
    }
  end

  def iter2
    yield
  end

  iter1{
    jb = 2
    iter1{
      jb = 3
    }
    jb
  } == 3
end

assert('BS Block 14') do
  def iter1
    iter2{
      yield
    }
  end

  def iter2
    yield
  end

  iter1{
    jb = 2
    iter1{
      jb
    }
    jb
  } == 2
end

assert('BS Block 15') do
  def m
    yield 1
  end
  m{|ib|
    ib*2
  } == 2
end

assert('BS Block 16') do
  def m
    yield 12345, 67890
  end
  m{|ib,jb|
    ib*2+jb
  } == 92580
end

assert('BS Block 17') do
  def iter
    yield 10
  end

  a = nil
  [iter{|a|
    a
  }, a] == [10, nil]
end

assert('BS Block 18') do
  def iter
    yield 10
  end

  iter{|a|
    iter{|a|
      a + 1
    } + a
  } == 21
end

assert('BS Block 19') do
  def iter
    yield 10, 20, 30, 40
  end

  a = b = c = d = nil
  iter{|a, b, c, d|
    [a, b, c, d]
  } + [a, b, c, d] == [10, 20, 30, 40, nil, nil, nil, nil]
end

assert('BS Block 20') do
  def iter
    yield 10, 20, 30, 40
  end

  a = b = nil
  iter{|a, b, c, d|
    [a, b, c, d]
  } + [a, b] == [10, 20, 30, 40, nil, nil]
end

assert('BS Block 21') do
  def iter
    yield 1, 2
  end

  iter{|a, *b|
    [a, b]
  } == [1, [2]]
end

assert('BS Block 22') do
  def iter
    yield 1, 2
  end

  iter{|*a|
    [a]
  } == [[1, 2]]
end

assert('BS Block 23') do
  def iter
    yield 1, 2
  end

  iter{|a, b, *c|
    [a, b, c]
  } == [1, 2, []]
end

assert('BS Block 24') do
  def m
    yield
  end
  m{
    1
  } == 1
end

assert('BS Block 25') do
  def m
    yield 123
  end
  m{|ib|
    m{|jb|
      ib*jb
    }
  } == 15129
end

assert('BS Block 26') do
  def m a
    yield a
  end
  m(1){|ib|
    m(2){|jb|
      ib*jb
    }
  } == 2
end

assert('BS Block 27') do
  sum = 0
  3.times{|ib|
    2.times{|jb|
      sum += ib + jb
    }}
  sum == 9
end

assert('BS Block 28') do
  3.times{|bl|
    break 10
  } == 10
end

assert('BS Block 29') do
  def iter
    yield 1,2,3
  end

  iter{|i, j|
    [i, j]
  } == [1, 2]
end

assert('BS Block 30') do
  def iter
    yield 1
  end

  iter{|i, j|
    [i, j]
  } == [1, nil]
end

assert('BS Block [ruby-dev:31147]') do
  def m
    yield
  end
  m{|&b| b} == nil
end

assert('BS Block [ruby-dev:31160]') do
  def m()
    yield
  end
  m {|(v,(*))|} == nil
end

assert('BS Block 31') do
  def m()
    yield
  end
  m {|((*))|} == nil
end

assert('BS Block [ruby-dev:31440]') do
  def m
    yield [0]
  end
  m{|v, &b| v} == [0]
end

assert('BS Block 32') do
  r = false; 1.times{|&b| r = b}; r.class == NilClass
end

assert('BS Block [ruby-core:14395]') do
  class Controller
    def respond_to(&block)
      responder = Responder.new
      block.call(responder)
      responder.respond
    end
    def test_for_bug
      respond_to{|format|
        format.js{
          "in test"
          render{|obj|
            obj
          }
        }
      }
    end
    def render(&block)
      "in render"
    end
  end

  class Responder
    def method_missing(symbol, &block)
      "enter method_missing"
      @response = Proc.new{
        'in method missing'
        block.call
      }
      "leave method_missing"
    end
    def respond
      @response.call
    end
  end
  t = Controller.new
  t.test_for_bug
end
##
# Bootstrap test for literals

assert('BS Literal 1') do
  true == true
end

assert('BS Literal 2') do
  TrueClass == true.class
end

assert('BS Literal 3') do
  false == false
end

assert('BS Literal 4') do
  FalseClass == false.class
end

assert('BS Literal 5') do
  'nil' == nil.inspect
end

assert('BS Literal 6') do
  NilClass == nil.class
end

assert('BS Literal 7') do
  Symbol == :sym.class
end

assert('BS Literal 8') do
  1234 == 1234
end

assert('BS Literal 9') do
  Fixnum == 1234.class
end
##
# Class ISO Test

assert('Class', '15.2.3') do
  Class.class == Class
end

assert('Class superclass', '15.2.3.2') do
  Class.superclass == Module
end

assert('Class#new', '15.2.3.3.3') do
  # at the moment no exception on singleton class
  #e1 = nil
  #begin
  #  class1 = e1.singleton_class.new
  #rescue => e1
  #  e2 = e1
  #end

  class TestClass
    def initialize args, &block
      @result = if not args.nil? and block.nil?
        # only arguments
        :only_args
      elsif not args.nil? and not block.nil?
        # args and block is given
        :args_and_block
      else
        # this should never happen
        :broken
      end
    end

    def result; @result; end
  end

  TestClass.new(:arg).result == :only_args
  # with block doesn't work yet
end

assert('Class#superclass', '15.2.3.3.4') do
  class SubClass < String; end
  SubClass.superclass == String
end

# Not ISO specified

assert('Class 1') do
  class C1; end
  C1.class == Class
end

assert('Class 2') do
  class C2; end
  C2.new.class == C2
end

assert('Class 3') do
  class C3; end
  C3.new.class.class == Class
end

assert('Class 4') do
  class C4_A; end
  class C4 < C4_A; end
  C4.class == Class
end

assert('Class 5') do
  class C5_A; end
  class C5 < C5_A; end
  C5.new.class == C5
end

assert('Class 6') do
  class C6_A; end
  class C6 < C6_A; end
  C6.new.class.class == Class
end

assert('Class 7') do
  class C7_A; end
  class C7_B; end

  class C7 < C7_A; end

  error = false
  begin
    # Different superclass.
    class C7 < C7_B; end
  rescue TypeError
    error = true
  end

  error
end

assert('Class 8') do
  class C8_A; end

  class C8; end  # superclass is Object

  error = false
  begin
    # Different superclass.
    class C8 < C8_A; end
  rescue TypeError
    error = true
  end

  error
end

assert('Class 9') do
  Class9Const = "a"

  error = false
  begin
    class Class9Const; end
  rescue TypeError
    error = true
  end

  error
end

assert('Class Module 1') do
  module M; end
  M.class == Module
end

assert('Class Module 2') do
  module M; end
  class C; include M; end
  C.new.class == C
end

# nested class
assert('Class Nested 1') do
  class A; end
  class A::B; end
  A::B == A::B
end

assert('Class Nested 2') do
  class A; end
  class A::B; end
  A::B.new.class == A::B
end

assert('Class Nested 3') do
  class A; end
  class A::B; end
  A::B.new.class.class == Class
end

assert('Class Nested 4') do
  class A; end
  class A::B; end
  class A::B::C; end
  A::B::C == A::B::C
end

assert('Class Nested 5') do
  class A; end
  class A::B; end
  class A::B::C; end
  A::B::C.class == Class
end

assert('Class Nested 6') do
  class A; end
  class A::B; end
  class A::B::C; end
  A::B::C.new.class == A::B::C
end

assert('Class Nested 7') do
  class A; end
  class A::B; end
  class A::B2 < A::B; end
  A::B2 == A::B2
end

assert('Class Nested 8') do
  class A; end
  class A::B; end
  class A::B2 < A::B; end
  A::B2.class == Class
end

assert('Class Colon 1') do
  class A; end; A::C = 1; A::C == 1
end

assert('Class Colon 2') do
  class A; class ::C; end end; C == C
end

assert('Class Colon 3') do
  class A; class ::C; end end; C.class == Class
end

assert('Class Dup 1') do
  class C; end;  C.dup.class == Class
end

assert('Class Dup 2') do
  module M; end;  M.dup.class == Module
end

assert('<', '15.3.3.2.1') do
  class Foo
    include Comparable
    def <=>(x)
      0
    end
  end

  (Foo.new < Foo.new) == false
end

assert('<=', '15.3.3.2.2') do
  class Foo
    include Comparable
    def <=>(x)
      0
    end
  end

  (Foo.new <= Foo.new) == true
end

assert('==', '15.3.3.2.3') do
  class Foo
    include Comparable
    def <=>(x)
      0
    end
  end

  (Foo.new == Foo.new) == true
end

assert('>', '15.3.3.2.4') do
  class Foo
    include Comparable
    def <=>(x)
      0
    end
  end

  (Foo.new > Foo.new) == false
end

assert('>=', '15.3.3.2.5') do
  class Foo
    include Comparable
    def <=>(x)
      0
    end
  end

  (Foo.new >= Foo.new) == true
end

##
# Enumerable ISO Test

assert('Enumerable', '15.3.2') do
  Enumerable.class == Module
end

assert('Enumerable#all?', '15.3.2.2.1') do
  [1,2,3].all? and not [1,false,3].all?
end

assert('Enumerable#any?', '15.3.2.2.2') do
  [false,true,false].any? and not [false,false,false].any?
end

assert('Enumerable#collect', '15.3.2.2.3') do
  [1,2,3].collect { |i| i + i } == [2,4,6]
end

assert('Enumerable#detect', '15.3.2.2.4') do
  [1,2,3].detect() { true } and [1,2,3].detect("a") { false } == 'a'
end

assert('Array#each_with_index', '15.3.2.2.5') do
  a = nil
  b = nil

  [1].each_with_index {|e,i| a = e; b = i}

  a == 1 and b == 0
end

assert('Enumerable#entries', '15.3.2.2.6') do
  [1].entries == [1]
end

assert('Enumerable#find', '15.3.2.2.7') do
  [1,2,3].find() { true } and [1,2,3].find("a") { false } == 'a'
end

assert('Enumerable#find_all', '15.3.2.2.8') do
  [1,2,3,4,5,6,7,8,9].find_all() {|i| i%2 == 0} == [2,4,6,8]
end

assert('Enumerable#grep', '15.3.2.2.9') do
  [1,2,3,4,5,6,7,8,9].grep(4..6) == [4,5,6]
end

assert('Enumerable#include?', '15.3.2.2.10') do
  [1,2,3,4,5,6,7,8,9].include?(5) and
    not [1,2,3,4,5,6,7,8,9].include?(0)
end

assert('Enumerable#inject', '15.3.2.2.11') do
  [1,2,3,4,5,6].inject() {|s, n| s + n} == 21 and
    [1,2,3,4,5,6].inject(1) {|s, n| s + n} == 22
end

assert('Enumerable#map', '15.3.2.2.12') do
  [1,2,3].map { |i| i + i } == [2,4,6]
end

assert('Enumerable#max', '15.3.2.2.13') do
  a = ['aaa', 'bb', 'c']
  a.max == 'c' and
    a.max {|i1,i2| i1.length <=> i2.length} == 'aaa'
end

assert('Enumerable#min', '15.3.2.2.14') do
  a = ['aaa', 'bb', 'c']
  a.min == 'aaa' and
    a.min {|i1,i2| i1.length <=> i2.length} == 'c'
end

assert('Enumerable#member?', '15.3.2.2.15') do
  [1,2,3,4,5,6,7,8,9].member?(5) and
    not [1,2,3,4,5,6,7,8,9].member?(0)
end

assert('Enumerable#partion', '15.3.2.2.16') do
  [0,1,2,3,4,5,6,7,8,9].partition do |i|
    i % 2 == 0
  end == [[0,2,4,6,8], [1,3,5,7,9]]
end

assert('Enumerable#reject', '15.3.2.2.17') do
  [0,1,2,3,4,5,6,7,8,9].reject do |i|
    i % 2 == 0
  end == [1,3,5,7,9]
end

assert('Enumerable#select', '15.3.2.2.18') do
  [1,2,3,4,5,6,7,8,9].select() {|i| i%2 == 0} == [2,4,6,8]
end

assert('Enumerable#sort', '15.3.2.2.19') do
  [7,3,1,2,6,4].sort == [1,2,3,4,6,7] and
    [7,3,1,2,6,4].sort {|e1,e2| e2<=>e1} == [7,6,4,3,2,1]
end

assert('Enumerable#to_a', '15.3.2.2.20') do
  [1].to_a == [1]
end
##
# Exception ISO Test

assert('Exception', '15.2.22') do
  Exception.class == Class
end

assert('Exception superclass', '15.2.22.2') do
  Exception.superclass == Object
end

assert('Exception.exception', '15.2.22.4.1') do
  e = Exception.exception('a')

  e.class == Exception
end

assert('Exception#exception', '15.2.22.5.1') do
  e1 = Exception.exception()
  e2 = Exception.exception('b')

  e1.class == Exception and e2.class == Exception
end

assert('Exception#message', '15.2.22.5.2') do
  e = Exception.exception('a')

  e.message == 'a'
end

assert('Exception#to_s', '15.2.22.5.3') do
  e = Exception.exception('a')
  
  e.to_s == 'a'
end

assert('Exception.exception', '15.2.22.4.1') do
  e = Exception.exception()
  e.initialize('a')

  e.message == 'a'
end

assert('ScriptError', '15.2.37') do
  begin
    raise ScriptError.new
  rescue ScriptError
    true
  else
    false
  end
end

assert('SyntaxError', '15.2.38') do
  begin
    raise SyntaxError.new
  rescue SyntaxError
    true
  else
    false
  end
end

# Not ISO specified

assert('Exception 1') do
  begin
    1+1
  ensure
    2+2
  end == 2
end

assert('Exception 2') do
  begin
    1+1
    begin
      2+2
    ensure
      3+3
    end
  ensure
    4+4
  end == 4
end

assert('Exception 3') do
  begin
    1+1
    begin
      2+2
    ensure
      3+3
    end
  ensure
    4+4
    begin
      5+5
    ensure
      6+6
    end
  end == 4
end

assert('Exception 4') do
  a = nil
  1.times{|e|
    begin
    rescue => err
    end
    a = err.class
  }
  a == NilClass
end

assert('Exception 5') do
  $ans = []
  def m
    $!
  end
  def m2
    1.times{
      begin
        return
      ensure
        $ans << m
      end
    }
  end
  m2
  $ans == [nil]
end

assert('Exception 6') do
  $i = 0
  def m
    iter{
      begin
        $i += 1
        begin
          $i += 2
          break
        ensure

        end
      ensure
        $i += 4
      end
      $i = 0
    }
  end

  def iter
    yield
  end
  m
  $i == 7
end

assert('Exception 7') do
  $i = 0
  def m
    begin
      $i += 1
      begin
        $i += 2
        return
      ensure
        $i += 3
      end
    ensure
      $i += 4
    end
    p :end
  end
  m
  $i == 10
end

assert('Exception 8') do
  begin
    1
  rescue
    2
  else
    3
  end == 3
end

assert('Exception 9') do
  begin
    1+1
  rescue
    2+2
  else
    3+3
  ensure
    4+4
  end == 6
end

assert('Exception 10') do
  begin
    1+1
    begin
      2+2
    rescue
      3+3
    else
      4+4
    end
  rescue
    5+5
  else
    6+6
  ensure
    7+7
  end == 12
end

assert('Exception 11') do
  a = :ok
  begin
    begin
      raise Exception
    rescue
      a = :ng
    end
  rescue Exception
  end
  a == :ok
end

assert('Exception 12') do
  a = :ok
  begin
    raise Exception rescue a = :ng
  rescue Exception
  end
  a == :ok
end

assert('Exception 13') do
  a = :ng
  begin
    raise StandardError
  rescue TypeError, ArgumentError
    a = :ng
  rescue
    a = :ok
  else
    a = :ng
  end
  a == :ok
end

def exception_test14
  UnknownConstant
end

assert('Exception 14') do
  a = :ng
  begin
    send(:exception_test14)
  rescue
    a = :ok
  end

  a == :ok
end

##
# FalseClass ISO Test

assert('FalseClass', '15.2.6') do
  FalseClass.class == Class
end

assert('FalseClass superclass', '15.2.6.2') do
  FalseClass.superclass == Object
end

assert('FalseClass false', '15.2.6.1') do
  not false
end

assert('FalseClass#&', '15.2.6.3.1') do
  not false.&(true) and not false.&(false)
end

assert('FalseClass#^', '15.2.6.3.2') do
  false.^(true) and not false.^(false)
end

assert('FalseClass#to_s', '15.2.6.3.3') do
  false.to_s == 'false'
end

assert('FalseClass#|', '15.2.6.3.4') do
  false.|(true) and not false.|(false)
end
##
# Float ISO Test

assert('Float', '15.2.9') do
  Float.class == Class
end

assert('Float superclass', '15.2.9.2') do
  Float.superclass == Numeric
end

assert('Float#+', '15.2.9.3.1') do
  a = 3.123456788 + 0.000000001
  b = 3.123456789 + 1

  check_float(a, 3.123456789) and
    check_float(b, 4.123456789)
end

assert('Float#-', '15.2.9.3.2') do
  a = 3.123456790 - 0.000000001
  b = 5.123456789 - 1

  check_float(a, 3.123456789) and
    check_float(b, 4.123456789)
end

assert('Float#*', '15.2.9.3.3') do
  a = 3.125 * 3.125
  b = 3.125 * 1

  check_float(a, 9.765625) and
    check_float(b, 3.125)
end

assert('Float#/', '15.2.9.3.4') do
  a = 3.123456789 / 3.123456789
  b = 3.123456789 / 1

  check_float(a, 1.0) and
    check_float(b, 3.123456789)
end

assert('Float#%', '15.2.9.3.5') do
  a = 3.125 % 3.125
  b = 3.125 % 1

  check_float(a, 0.0) and
    check_float(b, 0.125)
end

assert('Float#<=>', '15.2.9.3.6') do
  a = 3.125 <=> 3.123
  b = 3.125 <=> 3.125
  c = 3.125 <=> 3.126
  a2 = 3.125 <=> 3
  c2 = 3.125 <=> 4

  a == 1 and b == 0 and c == -1 and
    a2 == 1 and c2 == -1
end

assert('Float#==', '15.2.9.3.7') do
  3.1 == 3.1 and not 3.1 == 3.2
end

assert('Float#ceil', '15.2.9.3.8') do
  a = 3.123456789.ceil
  b = 3.0.ceil
  c = -3.123456789.ceil
  d = -3.0.ceil
  a == 4 and b == 3 and c == -3 and d == -3
end

assert('Float#finite?', '15.2.9.3.9') do
  3.123456789.finite? and
    not (1.0 / 0.0).finite?
end

assert('Float#floor', '15.2.9.3.10') do
  a = 3.123456789.floor
  b = 3.0.floor
  c = -3.123456789.floor
  d = -3.0.floor
  a == 3 and b == 3 and c == -4 and d == -3
end

assert('Float#infinite?', '15.2.9.3.11') do
  a = 3.123456789.infinite?
  b = (1.0 / 0.0).infinite?
  c = (-1.0 / 0.0).infinite?

  a == nil and b == 1 and c == -1
end

assert('Float#round', '15.2.9.3.12') do
  a = 3.123456789.round
  b = 3.5.round
  c = 3.4999.round
  d = (-3.123456789).round
  e = (-3.5).round
  f = 12345.67.round(-1)
  g = 3.423456789.round(0)
  h = 3.423456789.round(1)
  i = 3.423456789.round(3)

  a == 3 and b == 4 and c == 3 and d == -3 and e == -4 and
    f == 12350 and g == 3 and h == 3.4 and i == 3.423
end

assert('Float#to_f', '15.2.9.3.13') do
  a = 3.123456789

  check_float(a.to_f, a)
end

assert('Float#to_i', '15.2.9.3.14') do
  3.123456789.to_i == 3
end

assert('Float#truncate', '15.2.9.3.15') do
  3.123456789.truncate == 3 and -3.1.truncate == -3
end
##
# Hash ISO Test

assert('Hash', '15.2.13') do
  Hash.class == Class
end

assert('Hash superclass', '15.2.13.2') do
  Hash.superclass == Object
end

assert('Hash#==', '15.2.13.4.1') do
  ({ 'abc' => 'abc' } ==  { 'abc' => 'abc' }) and
    not ({ 'abc' => 'abc' } ==  { 'cba' => 'cba' })
end

assert('Hash#[]', '15.2.13.4.2') do
  a = { 'abc' => 'abc' }

  a['abc'] == 'abc'
end

assert('Hash#[]=', '15.2.13.4.3') do
  a = Hash.new
  a['abc'] = 'abc'

  a['abc'] == 'abc'
end

assert('Hash#clear', '15.2.13.4.4') do
  a = { 'abc' => 'abc' }
  a.clear

  a == { }
end

assert('Hash#default', '15.2.13.4.5') do
  a = Hash.new
  b = Hash.new('abc')
  c = Hash.new {|s,k| s[k] = k}

  a.default == nil and b.default == 'abc' and
    c.default == nil and c.default('abc') == 'abc'
end

assert('Hash#default=', '15.2.13.4.6') do
  a = { 'abc' => 'abc' }
  a.default = 'cba'

  a['abc'] == 'abc' and a['notexist'] == 'cba'
end

assert('Hash#default_proc', '15.2.13.4.7') do
  a = Hash.new
  b = Hash.new {|s,k| s[k] = k}

  a.default_proc == nil and b.default_proc.class == Proc
end

assert('Hash#delete', '15.2.13.4.8') do
  a = { 'abc' => 'abc' }
  b = { 'abc' => 'abc' }
  b_tmp_1 = false
  b_tmp_2 = false

  a.delete('abc')
  b.delete('abc') do |k|
    b_tmp_1 = true
  end
  b.delete('abc') do |k|
    b_tmp_2 = true
  end

  a.delete('cba') == nil and not a.has_key?('abc') and
    not b_tmp_1 and b_tmp_2
end

assert('Hash#each', '15.2.13.4.9') do
  a = { 'abc_key' => 'abc_value' }
  key = nil
  value = nil

  a.each  do |k,v|
    key = k
    value = v
  end

  key == 'abc_key' and value == 'abc_value'
end

assert('Hash#each_key', '15.2.13.4.10') do
  a = { 'abc_key' => 'abc_value' }
  key = nil

  a.each_key  do |k|
    key = k
  end

  key == 'abc_key'
end

assert('Hash#each_value', '15.2.13.4.11') do
  a = { 'abc_key' => 'abc_value' }
  value = nil

  a.each_value  do |v|
    value = v
  end

  value == 'abc_value'
end

assert('Hash#empty?', '15.2.13.4.12') do
  a = { 'abc_key' => 'abc_value' }
  b = Hash.new

  not a.empty? and b.empty?
end

assert('Hash#has_key?', '15.2.13.4.13') do
  a = { 'abc_key' => 'abc_value' }
  b = Hash.new

  a.has_key?('abc_key') and not b.has_key?('cba')
end

assert('Hash#has_value?', '15.2.13.4.14') do
  a = { 'abc_key' => 'abc_value' }
  b = Hash.new

  a.has_value?('abc_value') and not b.has_value?('cba')
end

assert('Hash#include?', '15.2.13.4.15') do
  a = { 'abc_key' => 'abc_value' }
  b = Hash.new

  a.include?('abc_key') and not b.include?('cba')
end

assert('Hash#initialize copy', '15.2.13.4.17') do
  a = { 'abc_key' => 'abc_value' }
  b = Hash.new.initialize_copy(a)

  b == { 'abc_key' => 'abc_value' }
end

assert('Hash#key?', '15.2.13.4.18') do
  a = { 'abc_key' => 'abc_value' }
  b = Hash.new

  a.key?('abc_key') and not b.key?('cba')
end

assert('Hash#keys', '15.2.13.4.19') do
  a = { 'abc_key' => 'abc_value' }

  a.keys == ['abc_key']
end

assert('Hash#length', '15.2.13.4.20') do
  a = { 'abc_key' => 'abc_value' }
  b = Hash.new

  a.length == 1 and b.length == 0
end

assert('Hash#member?', '15.2.13.4.21') do
  a = { 'abc_key' => 'abc_value' }
  b = Hash.new

  a.member?('abc_key') and not b.member?('cba')
end

assert('Hash#merge', '15.2.13.4.22') do
  a = { 'abc_key' => 'abc_value', 'cba_key' => 'cba_value' }
  b = { 'cba_key' => 'XXX',  'xyz_key' => 'xyz_value' }

  result_1 = a.merge b
  result_2 = a.merge(b) do |key, original, new|
    original
  end

  result_1 == {'abc_key' => 'abc_value', 'cba_key' => 'XXX',
               'xyz_key' => 'xyz_value' } and
  result_2 == {'abc_key' => 'abc_value', 'cba_key' => 'cba_value',
               'xyz_key' => 'xyz_value' }
end

assert('Hash#replace', '15.2.13.4.23') do
  a = { 'abc_key' => 'abc_value' }
  b = Hash.new.replace(a)

  b == { 'abc_key' => 'abc_value' }
end

assert('Hash#shift', '15.2.13.4.24') do
  a = { 'abc_key' => 'abc_value', 'cba_key' => 'cba_value' }
  b = a.shift

  a == { 'abc_key' => 'abc_value' } and
    b == [ 'cba_key', 'cba_value' ]
end

assert('Hash#size', '15.2.13.4.25') do
  a = { 'abc_key' => 'abc_value' }
  b = Hash.new

  a.size == 1 and b.size == 0
end

assert('Hash#store', '15.2.13.4.26') do
  a = Hash.new
  a.store('abc', 'abc')

  a['abc'] == 'abc'
end

assert('Hash#value?', '15.2.13.4.27') do
  a = { 'abc_key' => 'abc_value' }
  b = Hash.new

  a.value?('abc_value') and not b.value?('cba')
end

assert('Hash#values', '15.2.13.4.28') do
  a = { 'abc_key' => 'abc_value' }

  a.values == ['abc_value']
end

# Not ISO specified

assert('Hash#reject') do
  h = {:one => 1, :two => 2, :three => 3, :four => 4}
  ret = h.reject do |k,v|
    v % 2 == 0
  end
  ret == {:one => 1, :three => 3} and
    h == {:one => 1, :two => 2, :three => 3, :four => 4}
end

assert('Hash#reject!') do
  h = {:one => 1, :two => 2, :three => 3, :four => 4}
  ret = h.reject! do |k,v|
    v % 2 == 0
  end
  ret == {:one => 1, :three => 3} and
    h == {:one => 1, :three => 3}
end

assert('Hash#select') do
  h = {:one => 1, :two => 2, :three => 3, :four => 4}
  ret = h.select do |k,v|
    v % 2 == 0
  end
  ret == {:two => 2, :four => 4} and
    h == {:one => 1, :two => 2, :three => 3, :four => 4}
end

assert('Hash#select!') do
  h = {:one => 1, :two => 2, :three => 3, :four => 4}
  ret = h.select! do |k,v|
    v % 2 == 0
  end
  ret == {:two => 2, :four => 4} and
    h == {:two => 2, :four => 4}
end

##
# IndexError ISO Test

assert('IndexError', '15.2.33') do
  IndexError.class == Class
end

assert('IndexError superclass', '15.2.33.2') do
  IndexError.superclass == StandardError
end

##
# Integer ISO Test

assert('Integer', '15.2.8') do
  Integer.class == Class
end

assert('Integer superclass', '15.2.8.2') do
  Integer.superclass == Numeric
end

assert('Integer#+', '15.2.8.3.1') do
  a = 1+1
  b = 1+1.0

  a == 2 and b == 2.0
end

assert('Integer#-', '15.2.8.3.2') do
  a = 2-1
  b = 2-1.0

  a == 1 and b == 1.0
end

assert('Integer#*', '15.2.8.3.3') do
  a = 1*1
  b = 1*1.0

  a == 1 and b == 1.0
end

assert('Integer#/', '15.2.8.3.4') do
  a = 2/1
  b = 2/1.0

  a == 2 and b == 2.0
end

assert('Integer#%', '15.2.8.3.5') do
  a = 1%1
  b = 1%1.0
  c = 2%4

  a == 0 and b == 0.0 and c == 2
end

assert('Integer#<=>', '15.2.8.3.6') do
  a = 1<=>0
  b = 1<=>1
  c = 1<=>2

  a == 1 and b == 0 and c == -1
end

assert('Integer#==', '15.2.8.3.7') do
  a = 1==0
  b = 1==1

  a == false and b == true
end

assert('Integer#~', '15.2.8.3.8') do
  # Complement
  ~0 == -1 and ~2 == -3
end

assert('Integer#&', '15.2.8.3.9') do
  # Bitwise AND
  #   0101 (5)
  # & 0011 (3)
  # = 0001 (1)
  5 & 3 == 1
end

assert('Integer#|', '15.2.8.3.10') do
  # Bitwise OR
  #   0101 (5)
  # | 0011 (3)
  # = 0111 (7)
  5 | 3 == 7
end

assert('Integer#^', '15.2.8.3.11') do
  # Bitwise XOR
  #   0101 (5)
  # ^ 0011 (3)
  # = 0110 (6)
  5 ^ 3 == 6
end

assert('Integer#<<', '15.2.8.3.12') do
  # Left Shift by one
  #   00010111 (23)
  # = 00101110 (46)
  23 << 1 == 46
end

assert('Integer#>>', '15.2.8.3.13') do
  # Right Shift by one
  #   00101110 (46)
  # = 00010111 (23)
  46 >> 1 == 23
end

assert('Integer#ceil', '15.2.8.3.14') do
  10.ceil == 10
end

assert('Integer#downto', '15.2.8.3.15') do
  a = 0
  3.downto(1) do |i|
    a += i
  end
  a == 6
end

assert('Integer#eql?', '15.2.8.3.16') do
  a = 1.eql?(1)
  b = 1.eql?(2)
  c = 1.eql?(nil)

  a == true and b == false and c == false
end

assert('Integer#floor', '15.2.8.3.17') do
  a = 1.floor

  a == 1
end

assert('Integer#next', '15.2.8.3.19') do
  1.next == 2
end

assert('Integer#round', '15.2.8.3.20') do
  1.round == 1
end

assert('Integer#succ', '15.2.8.3.21') do
  1.succ == 2
end

assert('Integer#times', '15.2.8.3.22') do
  a = 0
  3.times do
    a += 1
  end
  a == 3
end

assert('Integer#to_f', '15.2.8.3.23') do
  1.to_f == 1.0
end

assert('Integer#to_i', '15.2.8.3.24') do
  1.to_i == 1
end

assert('Integer#to_s', '15.2.8.3.25') do
  1.to_s == '1' and -1.to_s == "-1"
end

assert('Integer#truncate', '15.2.8.3.26') do
  1.truncate == 1
end

assert('Integer#upto', '15.2.8.3.27') do
  a = 0
  1.upto(3) do |i|
    a += i
  end
  a == 6
end
##
# Kernel ISO Test

assert('Kernel', '15.3.1') do
  Kernel.class == Module
end

assert('Kernel.block_given?', '15.3.1.2.2') do
  def bg_try(&b)
    if Kernel.block_given?
      yield
    else
      "no block"
    end
  end

  (Kernel.block_given? == false) and
    # test without block
    (bg_try == "no block") and
    # test with block
    ((bg_try { "block" }) == "block") and
    # test with block
    ((bg_try do "block" end) == "block")
end

assert('Kernel.global_variables', '15.3.1.2.4') do
  Kernel.global_variables.class == Array
end

assert('Kernel.iterator?', '15.3.1.2.5') do
  Kernel.iterator? == false
end

assert('Kernel.lambda', '15.3.1.2.6') do
  l = Kernel.lambda do
    true
  end

  m = Kernel.lambda(&l)

  l.call and l.class == Proc and m.call and m.class == Proc
end

# Not implemented at the moment
#assert('Kernel.local_variables', '15.3.1.2.7') do
#  Kernel.local_variables.class == Array
#end

assert('Kernel.loop', '15.3.1.2.8') do
  i = 0

  Kernel.loop do
    i += 1
    break if i == 100
  end

  i == 100
end

assert('Kernel.p', '15.3.1.2.9') do
  # TODO search for a way to test p to stdio
  true
end

assert('Kernel.print', '15.3.1.2.10') do
  # TODO search for a way to test print to stdio
  true
end

assert('Kernel.puts', '15.3.1.2.11') do
  # TODO search for a way to test puts to stdio
  true
end

assert('Kernel.raise', '15.3.1.2.12') do
  e_list = []

  begin
    Kernel.raise
  rescue => e
    e_list << e
  end

  begin
    Kernel.raise RuntimeError.new
  rescue => e
    e_list << e
  end

  # result without argument
  e_list[0].class == RuntimeError and
    # result with RuntimeError argument
    e_list[1].class == RuntimeError
end

assert('Kernel#__id__', '15.3.1.3.3') do
  __id__.class == Fixnum
end

assert('Kernel#__send__', '15.3.1.3.4') do
  # test with block
  l = __send__(:lambda) do
    true
  end
 
  l.call and l.class == Proc and
    # test with argument
    __send__(:respond_to?, :nil?) and
    # test without argument and without block
    __send__(:public_methods).class == Array
end

assert('Kernel#block_given?', '15.3.1.3.6') do
  def bg_try(&b)
    if block_given?
      yield
    else
      "no block"
    end
  end

  (block_given? == false) and
    (bg_try == "no block") and
    ((bg_try { "block" }) == "block") and
    ((bg_try do "block" end) == "block")
end

assert('Kernel#class', '15.3.1.3.7') do
  Kernel.class == Module
end

assert('Kernel#clone', '15.3.1.3.8') do
  class KernelCloneTest
    def initialize
      @v = 0
    end

    def get
      @v
    end

    def set(v)
      @v = v
    end
  end

  a = KernelCloneTest.new
  a.set(1)
  b = a.clone

  def a.test
  end
  a.set(2)
  c = a.clone

  a.get == 2 and b.get == 1 and c.get == 2 &&
    a.respond_to?(:test) == true and
    b.respond_to?(:test) == false and
    c.respond_to?(:test) == true
end

assert('Kernel#dup', '15.3.1.3.9') do
  class KernelDupTest
    def initialize
      @v = 0
    end

    def get
      @v
    end

    def set(v)
      @v = v
    end
  end

  a = KernelDupTest.new
  a.set(1)
  b = a.dup

  def a.test
  end
  a.set(2)
  c = a.dup

  a.get == 2 and b.get == 1 and c.get == 2 and
    a.respond_to?(:test) == true and
    b.respond_to?(:test) == false and
    c.respond_to?(:test) == false
end

assert('Kernel#extend', '15.3.1.3.13') do
  class Test4ExtendClass
  end

  module Test4ExtendModule
    def test_method; end
  end

  a = Test4ExtendClass.new
  a.extend(Test4ExtendModule)
  b = Test4ExtendClass.new

  a.respond_to?(:test_method) == true && b.respond_to?(:test_method) == false
end

assert('Kernel#global_variables', '15.3.1.3.14') do
  global_variables.class == Array
end

assert('Kernel#hash', '15.3.1.3.15') do
  hash == hash
end

assert('Kernel#inspect', '15.3.1.3.17') do
  s = nil.inspect
  s.class == String and s == "nil"
end

assert('Kernel#instance_variables', '15.3.1.3.23') do
  o = Object.new
  o.instance_eval do
    @a = 11
    @b = 12
  end
  ivars = o.instance_variables
  ivars.class == Array and ivars.size == 2 and ivars.include?(:@a) and ivars.include?(:@b)
end

assert('Kernel#is_a?', '15.3.1.3.24') do
  is_a?(Kernel) and not is_a?(Array)
end

assert('Kernel#iterator?', '15.3.1.3.25') do
  iterator? == false
end

assert('Kernel#kind_of?', '15.3.1.3.26') do
  kind_of?(Kernel) and not kind_of?(Array)
end

assert('Kernel#lambda', '15.3.1.3.27') do
  l = lambda do
    true
  end

  m = lambda(&l)

  l.call and l.class == Proc and m.call and m.class == Proc
end

# Not implemented yet
#assert('Kernel#local_variables', '15.3.1.3.28') do
#  local_variables.class == Array
#end

assert('Kernel#loop', '15.3.1.3.29') do
  i = 0

  loop do
    i += 1
    break if i == 100
  end

  i == 100
end

assert('Kernel#methods', '15.3.1.3.31') do
  methods.class == Array
end

assert('Kernel#nil?', '15.3.1.3.32') do
  nil.nil? == true
end

assert('Kernel#object_id', '15.3.1.3.33') do
  object_id.class == Fixnum
end

assert('Kernel#private_methods', '15.3.1.3.36') do
  private_methods.class == Array
end

assert('Kernel#protected_methods', '15.3.1.3.37') do
  protected_methods.class == Array
end

assert('Kernel#public_methods', '15.3.1.3.38') do
  public_methods.class == Array
end

assert('Kernel#raise', '15.3.1.3.40') do
  e_list = []

  begin
    raise
  rescue => e
    e_list << e
  end

  begin
    raise RuntimeError.new
  rescue => e
    e_list << e
  end

  # result without argument
  e_list[0].class == RuntimeError and
    # result with RuntimeError argument
    e_list[1].class == RuntimeError
end

assert('Kernel#respond_to?', '15.3.1.3.43') do
  class Test4RespondTo
    def test_method; end
    undef test_method
  end

  respond_to?(:nil?) and Test4RespondTo.new.respond_to?(:test_method) == false
end

assert('Kernel#send', '15.3.1.3.44') do
  # test with block
  l = send(:lambda) do
    true
  end

  l.call and l.class == Proc and
    # test with argument
    send(:respond_to?, :nil?) and
    # test without argument and without block
    send(:public_methods).class == Array
end

assert('Kernel#singleton_methods', '15.3.1.3.45') do
  singleton_methods.class == Array
end

assert('Kernel#to_s', '15.3.1.3.46') do
  # TODO looks strange..
  to_s == ''
end
##
# Literals ISO Test

assert('Literals Numerical', '8.7.6.2') do
  # signed and unsigned integer
  1 == 1 and -1 == -1 and +1 == +1 and
    # signed and unsigned float
    1.0 == 1.0 and -1.0 == -1.0 and
    # binary
    0b10000000 == 128 and 0B10000000 == 128
    # octal
    0o10 == 8 and 0O10 == 8 and 0_10 == 8
    # hex
    0xff == 255 and 0Xff == 255 and
    # decimal
    0d999 == 999 and 0D999 == 999 and
    # decimal seperator
    10_000_000 == 10000000 and 1_0 == 10 and
    # integer with exponent
    1e1 == 10.0 and 1e-1 == 0.1 and 1e+1 == 10.0
    # float with exponent
    1.0e1 == 10.0 and 1.0e-1 == 0.1 and 1.0e+1 == 10.0
end

assert('Literals Strings Single Quoted', '8.7.6.3.2') do
  'abc' == 'abc' and '\'' == '\'' and '\\' == '\\'
end

assert('Literals Strings Double Quoted', '8.7.6.3.3') do
  a = "abc"

  "abc" == "abc" and "\"" == "\"" and "\\" == "\\" and
    "#{a}" == "abc"
end

assert('Literals Strings Quoted Non-Expanded', '8.7.6.3.4') do
  a = %q{abc}
  b = %q(abc)
  c = %q[abc]
  d = %q<abc>
  e = %q/abc/
  f = %q/ab\/c/

  a == 'abc' and b == 'abc' and c == 'abc' and d == 'abc' and
    e == 'abc' and f == 'ab/c'
end

assert('Literals Strings Quoted Expanded', '8.7.6.3.5') do
  a = %Q{abc}
  b = %Q(abc)
  c = %Q[abc]
  d = %Q<abc>
  e = %Q/abc/
  f = %Q/ab\/c/
  g = %Q{#{a}}

  a == 'abc' and b == 'abc' and c == 'abc' and d == 'abc' and
    e == 'abc' and f == 'ab/c' and g == 'abc'
end

# Not Implemented ATM assert('Literals Strings Here documents', '8.7.6.3.6') do

# Not Implemented ATM assert('Literals Array', '8.7.6.4') do

# Not Implemented ATM assert('Literals Regular expression', '8.7.6.5') do

# Not Implemented ATM assert('Literals Symbol', '8.7.6.6') do
##
# LocalJumpError ISO Test

assert('LocalJumoError', '15.2.25') do
  LocalJumpError.class == Class
end

# TODO 15.2.25.2.1 LocalJumpError#exit_value
# TODO 15.2.25.2.2 LocalJumpError#reason
##
# Math Test

if Object.const_defined?(:Math)
  assert('Math.sin 0') do
    check_float(Math.sin(0), 0)
  end

  assert('Math.sin PI/2') do
    check_float(Math.sin(Math::PI / 2), 1)
  end

  assert('Fundamental trig identities') do
    result = true
    N = 13
    N.times do |i|
      a  = Math::PI / N * i
      ca = Math::PI / 2 - a
      s  = Math.sin(a)
      c  = Math.cos(a)
      t  = Math.tan(a)
      result &= check_float(s, Math.cos(ca))
      result &= check_float(t, 1 / Math.tan(ca))
      result &= check_float(s ** 2 + c ** 2, 1)
      result &= check_float(t ** 2 + 1, (1/c) ** 2)
      result &= check_float((1/t) ** 2 + 1, (1/s) ** 2)
    end
    result
  end

  assert('Math.erf 0') do
    check_float(Math.erf(0), 0)
  end

  assert('Math.exp 0') do
    check_float(Math.exp(0), 1.0)
  end

  assert('Math.exp 1') do
    check_float(Math.exp(1), 2.718281828459045)
  end

  assert('Math.exp 1.5') do
    check_float(Math.exp(1.5), 4.4816890703380645)
  end

  assert('Math.log 1') do
    check_float(Math.log(1), 0)
  end

  assert('Math.log E') do
    check_float(Math.log(Math::E), 1.0)
  end

  assert('Math.log E**3') do
    check_float(Math.log(Math::E**3), 3.0)
  end

  assert('Math.log2 1') do
    check_float(Math.log2(1), 0.0)
  end

  assert('Math.log2 2') do
    check_float(Math.log2(2), 1.0)
  end

  assert('Math.log10 1') do
    check_float(Math.log10(1), 0.0)
  end

  assert('Math.log10 10') do
    check_float(Math.log10(10), 1.0)
  end

  assert('Math.log10 10**100') do
    check_float(Math.log10(10**100), 100.0)
  end

  assert('Math.sqrt') do
    num = [0.0, 1.0, 2.0, 3.0, 4.0]
    sqr = [0, 1, 4, 9, 16]
    result = true
    sqr.each_with_index do |v,i|
      result &= check_float(Math.sqrt(v), num[i])
    end
    result
  end

  assert('Math.cbrt') do
    num = [-2.0, -1.0, 0.0, 1.0, 2.0]
    cub = [-8, -1, 0, 1, 8]
    result = true
    cub.each_with_index do |v,i|
      result &= check_float(Math.cbrt(v), num[i])
    end
    result
  end

  assert('Math.hypot') do
    check_float(Math.hypot(3, 4), 5.0)
  end

  assert('Math.frexp 1234') do
    n = 1234
    fraction, exponent = Math.frexp(n)
    check_float(Math.ldexp(fraction, exponent), n)
  end

  assert('Math.erf 1') do
    check_float(Math.erf(1), 0.842700792949715)
  end

  assert('Math.erfc 1') do
    check_float(Math.erfc(1), 0.157299207050285)
  end
end

##
# Module ISO Test

assert('Module', '15.2.2') do
  Module.class == Class
end

assert('Module superclass', '15.2.2.2') do
  Module.superclass == Object
end

# TODO not implemented ATM assert('Module.constants', '15.2.2.3.1') do

# TODO not implemented ATM assert('Module.nesting', '15.2.2.3.2') do

assert('Module#ancestors', '15.2.2.4.9') do
  r = String.ancestors
  r.class == Array and r.include?(String) and r.include?(Object)
end

assert('Module#append_features', '15.2.2.4.10') do
  module Test4AppendFeatures
    def self.append_features(mod)
      Test4AppendFeatures2.const_set(:Const4AppendFeatures2, mod)
    end
  end
  module Test4AppendFeatures2
    include Test4AppendFeatures
  end

  Test4AppendFeatures2.const_get(:Const4AppendFeatures2) == Test4AppendFeatures2
end

assert('Module#const_defined?', '15.2.2.4.20') do
  module Test4ConstDefined
    Const4Test4ConstDefined = true
  end

  Test4ConstDefined.const_defined?(:Const4Test4ConstDefined) and
    not Test4ConstDefined.const_defined?(:NotExisting)
end

assert('Module#const_get', '15.2.2.4.21') do
  module Test4ConstGet
    Const4Test4ConstGet = 42
  end

  Test4ConstGet.const_get(:Const4Test4ConstGet) == 42
end

assert('Module.const_missing', '15.2.2.4.22') do
  e1 = nil

  module Test4ConstMissing
    def self.const_missing(sym)
      42 # the answer to everything
    end
  end

  Test4ConstMissing.const_get(:ConstDoesntExist) == 42
end

assert('Module#const_get', '15.2.2.4.23') do
  module Test4ConstSet
    Const4Test4ConstSet = 42
  end

  Test4ConstSet.const_set(:Const4Test4ConstSet, 23)
  Test4ConstSet.const_get(:Const4Test4ConstSet) == 23
end

assert('Module#include', '15.2.2.4.27') do
  module Test4Include
    Const4Include = 42
  end
  module Test4Include2
    include Test4Include
  end

  Test4Include2.const_get(:Const4Include) == 42
end

assert('Module#included', '15.2.2.4.29') do
  module Test4Included
    Const4Included = 42
    def self.included mod
      Test4Included.const_set(:Const4Included2, mod)
    end
  end
  module Test4Included2
    include Test4Included
  end

  Test4Included2.const_get(:Const4Included) == 42 and
    Test4Included2.const_get(:Const4Included2) == Test4Included2
end

assert('Module#included_modules', '15.2.2.4.30') do
  module Test4includedModules
  end
  module Test4includedModules2
    include Test4includedModules
  end

  r = Test4includedModules2.included_modules
  r.class == Array and r.include?(Test4includedModules)
end
##
# NameError ISO Test

assert('NameError', '15.2.31') do
  NameError.class == Class
end

assert('NameError superclass', '15.2.31.2') do
  NameError.superclass == StandardError
end

# TODO 15.2.31.2.1 NameError#name

assert('NameError#initialize', '15.2.31.2.2') do
   e = NameError.new.initialize('a')

   e.class == NameError and e.message == 'a'
end
##
# NilClass ISO Test

assert('NilClass', '15.2.4') do
  NilClass.class == Class
end

assert('NilClass#&', '15.2.4.3.1') do
  not nil.&(true) and not nil.&(nil)
end

assert('NilClass#^', '15.2.4.3.2') do
  nil.^(true) and not nil.^(false)
end

assert('NilClass#|', '15.2.4.3.3') do
  nil.|(true) and not nil.|(false)
end

assert('NilClass#nil?', '15.2.4.3.4') do
  nil.nil?
end

assert('NilClass#to_s', '15.2.4.3.5') do
  nil.to_s == ''
end
##
# NoMethodError ISO Test

assert('NoMethodError', '15.2.32') do
  e2 = nil
  begin
    doesNotExistAsAMethodNameForVerySure("")
  rescue => e1
    e2 = e1
  end

  NoMethodError.class == Class and e2.class == NoMethodError
end

assert('NoMethodError superclass', '15.2.32.2') do
  NoMethodError.superclass == NameError
end

##
# Numeric ISO Test

assert('Numeric', '15.2.7') do
  Numeric.class == Class
end

assert('Numeric superclass', '15.2.7.2') do
  Numeric.superclass == Object
end

assert('Numeric#+@', '15.2.7.4.1') do
  +1 == +1
end

assert('Numeric#-@', '15.2.7.4.2') do
  -1 == -1
end

assert('Numeric#abs', '15.2.7.4.3') do
  1.abs == 1 and -1.abs == 1.0
end

# Not ISO specified

assert('Numeric#**') do
  2.0**3 == 8.0
end
##
# Object ISO Test

assert('Object', '15.2.1') do
  Object.class == Class
end

assert('Object superclass', '15.2.1.2') do
  Object.superclass == BasicObject
end

##
# Proc ISO Test

assert('Proc', '15.2.17') do
  Proc.class == Class
end

assert('Proc superclass', '15.2.17.2') do
  Proc.superclass == Object
end

assert('Proc.new', '15.2.17.3.1') do
  a = nil

  begin
    Proc.new
  rescue => e
    a = e
  end

  b = Proc.new {}

  a.class == ArgumentError and b.class == Proc
end

assert('Proc#[]', '15.2.17.4.1') do
  a = 0
  b = Proc.new { a += 1 }
  b.[]

  a2 = 0
  b2 = Proc.new { |i| a2 += i }
  b2.[](5)

  a == 1 and a2 == 5
end

assert('Proc#call', '15.2.17.4.3') do
  a = 0
  b = Proc.new { a += 1 }
  b.call

  a2 = 0
  b2 = Proc.new { |i| a2 += i }
  b2.call(5)

  a == 1 and a2 == 5 
end
##
# Range ISO Test

assert('Range', '15.2.14') do
  Range.class == Class
end

assert('Range superclass', '15.2.14.2') do
  Range.superclass == Object
end

assert('Range#==', '15.2.14.4.1') do
  (1..10) == (1..10) and not (1..10) == (1..100)
end

assert('Range#===', '15.2.14.4.2') do
  a = (1..10)

  a === 5 and not a === 20
end

assert('Range#begin', '15.2.14.4.3') do
  (1..10).begin == 1
end

assert('Range#each', '15.2.14.4.4') do
  a = (1..3)
  b = 0
  a.each {|i| b += i}
  b == 6
end

assert('Range#end', '15.2.14.4.5') do
  (1..10).end == 10
end

assert('Range#exclude_end?', '15.2.14.4.6') do
  (1...10).exclude_end? and not (1..10).exclude_end?
end

assert('Range#first', '15.2.14.4.7') do
  (1..10).first == 1
end

assert('Range#include', '15.2.14.4.8') do
  a = (1..10)

  a.include?(5) and not a.include?(20)
end

# TODO SEGFAULT ATM
#assert('Range#initialize', '15.2.14.4.9') do
#  a = Range.new(1, 10, true)
#  b = Range.new(1, 10, false)
#
#  a == (1..10) and a.exclude_end? and b == (1..10) and
#    not b.exclude_end?
#end

assert('Range#last', '15.2.14.4.10') do
  (1..10).last == 10
end

assert('Range#member?', '15.2.14.4.11') do
  a = (1..10)

  a.member?(5) and not a.member?(20)
end
##
# RangeError ISO Test

assert('RangeError', '15.2.26') do
  RangeError.class == Class
end

assert('RangeError superclass', '15.2.26.2') do
  RangeError.superclass == StandardError
end

##
# RegexpError ISO Test

# TODO broken ATM assert('RegexpError', '15.2.27') do
##
# RuntimeError ISO Test

assert('RuntimeError', '15.2.28') do
  e2 = nil
  begin
    # this will cause an exception due to the wrong location
    retry
  rescue => e1
    e2 = e1
  end

  RuntimeError.class == Class and e2.class == RuntimeError
end
##
# StandardError ISO Test

assert('StandardError', '15.2.23') do
  StandardError.class == Class
end

assert('StandardError superclass', '15.2.23.2') do
  StandardError.superclass == Exception
end

##
# String ISO Test

assert('String', '15.2.10') do
  String.class == Class
end

assert('String superclass', '15.2.10.2') do
  String.superclass == Object
end

assert('String#*', '15.2.10.5.1') do
  'a' * 5 == 'aaaaa'
end

assert('String#+', '15.2.10.5.2') do
  'a' + 'b' == 'ab'
end

assert('String#<=>', '15.2.10.5.3') do
  a = '' <=> ''
  b = '' <=> 'not empty'
  c = 'not empty' <=> ''
  d = 'abc' <=> 'cba'
  e = 'cba' <=> 'abc'

  a == 0 and b == -1 and c == 1 and
    d == -1 and e == 1
end

assert('String#==', '15.2.10.5.4') do
  'abc' == 'abc' and not 'abc' == 'cba'
end

# TODO: SEGFAULT ATM assert('String#=~', '15.2.10.5.5')

assert('String#[]', '15.2.10.5.6') do
  # length of args is 1
  a = 'abc'[0]
  b = 'abc'[-1]
  c = 'abc'[10]
  d = 'abc'[-10]

  # length of args is 2
  a1 = 'abc'[0, -1]
  b1 = 'abc'[10, 0]
  c1 = 'abc'[-10, 0]
  d1 = 'abc'[0, 0]
  e1 = 'abc'[1, 2]

  # args is RegExp
  # TODO SEGFAULT ATM

  # args is String
  a3 = 'abc'['bc']
  b3 = 'abc'['XX']

  a == 'a' and b == 'c' and c == nil and d == nil and
    a1 == nil and b1 == nil and c1 == nil and d1 == '' and
    e1 == 'bc' and
    a3 == 'bc' and b3 == nil
end

assert('String#capitalize', '15.2.10.5.7') do
  a = 'abc'
  a.capitalize

  a == 'abc' and 'abc'.capitalize == 'Abc'
end

assert('String#capitalize!', '15.2.10.5.8') do
  a = 'abc'
  a.capitalize!

  a == 'Abc'
end

assert('String#chomp', '15.2.10.5.9') do
  a = 'abc'.chomp
  b = ''.chomp
  c = "abc\n".chomp
  d = "abc\n\n".chomp
  e = "abc\t".chomp("\t")
  f = "abc\n"

  f.chomp

  a == 'abc' and b == '' and c == 'abc' and
    d == "abc\n" and e == 'abc' and f == "abc\n"
end

assert('String#chomp!', '15.2.10.5.10') do
  a = 'abc'
  b = ''
  c = "abc\n"
  d = "abc\n\n"
  e = "abc\t"

  a.chomp!
  b.chomp!
  c.chomp!
  d.chomp!
  e.chomp!("\t")

  a == 'abc' and b == '' and c == 'abc' and
    d == "abc\n" and e == 'abc'
end

assert('String#chop', '15.2.10.5.11') do
  a = ''.chop
  b = 'abc'.chop
  c = 'abc'

  c.chop

  a == '' and b == 'ab' and c == 'abc'
end

assert('String#chop!', '15.2.10.5.12') do
  a = ''
  b = 'abc'

  a.chop!
  b.chop!

  a == '' and b == 'ab'
end

assert('String#downcase', '15.2.10.5.13') do
  a = 'ABC'.downcase
  b = 'ABC'

  b.downcase

  a == 'abc' and b == 'ABC'
end

assert('String#downcase!', '15.2.10.5.14') do
  a = 'ABC'

  a.downcase!

  a == 'abc'
end

assert('String#each_line', '15.2.10.5.15') do
  a = "first line\nsecond line\nthird line"
  list = ["first line\n", "second line\n", "third line"]
  n_list = []

  a.each_line do |line|
    n_list << line
  end

  list == n_list
end

assert('String#empty?', '15.2.10.5.16') do
  a = ''
  b = 'not empty'

  a.empty? and not b.empty?
end

assert('String#eql?', '15.2.10.5.17') do
  'abc'.eql?('abc') and not 'abc'.eql?('cba')
end

# TODO ATM broken assert('String#gsub', '15.2.10.5.18') do

# TODO ATM broken assert('String#gsub!', '15.2.10.5.19') do

assert('String#hash', '15.2.10.5.20') do
  a = 'abc'

  a.hash == 'abc'.hash
end

assert('String#include?', '15.2.10.5.21') do
  'abc'.include?(97) and not 'abc'.include?(100) and
    'abc'.include?('a') and not 'abc'.include?('d')
end

assert('String#index', '15.2.10.5.22') do
  'abc'.index('a') == 0 and 'abc'.index('d') == nil and
    'abcabc'.index('a', 1) == 3
end

assert('String#initialize', '15.2.10.5.23') do
  a = ''
  a.initialize('abc')

  a == 'abc'
end

assert('String#initialize_copy', '15.2.10.5.24') do
  a = ''
  a.initialize_copy('abc')

  a == 'abc'
end

assert('String#intern', '15.2.10.5.25') do
  'abc'.intern == :abc
end

assert('String#length', '15.2.10.5.26') do
  'abc'.length == 3
end

# TODO Broken ATM assert('String#match', '15.2.10.5.27') do

assert('String#replace', '15.2.10.5.28') do
  a = ''
  a.replace('abc')

  a == 'abc'
end

assert('String#reverse', '15.2.10.5.29') do
  a = 'abc'
  a.reverse

  a == 'abc' and 'abc'.reverse == 'cba'
end

assert('String#reverse!', '15.2.10.5.30') do
  a = 'abc'
  a.reverse!

  a == 'cba' and 'abc'.reverse! == 'cba'
end

assert('String#rindex', '15.2.10.5.31') do
  'abc'.rindex('a') == 0 and 'abc'.rindex('d') == nil and
    'abcabc'.rindex('a', 1) == 0 and 'abcabc'.rindex('a', 4) == 3
end

# TODO Broken ATM assert('String#scan', '15.2.10.5.32') do

assert('String#size', '15.2.10.5.33') do
  'abc'.size == 3
end

assert('String#slice', '15.2.10.5.34') do
  # length of args is 1
  a = 'abc'.slice(0)
  b = 'abc'.slice(-1)
  c = 'abc'.slice(10)
  d = 'abc'.slice(-10)

  # length of args is 2
  a1 = 'abc'.slice(0, -1)
  b1 = 'abc'.slice(10, 0)
  c1 = 'abc'.slice(-10, 0)
  d1 = 'abc'.slice(0, 0)
  e1 = 'abc'.slice(1, 2)

  # slice of shared string
  e11 = e1.slice(0)

  # args is RegExp
  # TODO SEGFAULT ATM

  # args is String
  a3 = 'abc'.slice('bc')
  b3 = 'abc'.slice('XX')

  a == 'a' and b == 'c' and c == nil and d == nil and
    a1 == nil and b1 == nil and c1 == nil and d1 == '' and
    e1 == 'bc' and e11 == 'b' and
    a3 == 'bc' and b3 == nil
end

# TODO Broken ATM
assert('String#split', '15.2.10.5.35') do
  # without RegExp behavior is actually unspecified
  'abc abc abc'.split == ['abc', 'abc', 'abc'] and
    'a,b,c,,d'.split(',') == ["a", "b", "c", "", "d"] and
    'abc abc abc'.split(nil) == ['abc', 'abc', 'abc'] and
    'abc'.split("") == ['a', 'b', 'c']
end

# TODO ATM broken assert('String#sub', '15.2.10.5.36') do

# TODO ATM broken assert('String#sub!', '15.2.10.5.37') do

assert('String#to_i', '15.2.10.5.38') do
  a = ''.to_i
  b = '123456789'.to_i
  c = 'a'.to_i(16)
  d = '100'.to_i(2)

  a == 0 and b == 123456789 and c == 10 and d == 4
end

assert('String#to_f', '15.2.10.5.39') do
  a = ''.to_f
  b = '123456789'.to_f
  c = '12345.6789'.to_f

  check_float(a, 0.0) and check_float(b, 123456789.0) and
    check_float(c, 12345.6789)
end

assert('String#to_s', '15.2.10.5.40') do
  'abc'.to_s == 'abc'
end

assert('String#to_sym', '15.2.10.5.41') do
  'abc'.to_sym == :abc
end

assert('String#upcase', '15.2.10.5.42') do
  a = 'abc'.upcase
  b = 'abc'

  b.upcase

  a == 'ABC' and b == 'abc'
end

assert('String#upcase!', '15.2.10.5.43') do
  a = 'abc'

  a.upcase!

  a == 'ABC'
end

# Not ISO specified

assert('String interpolation (mrb_str_concat for shared strings)') do
  a = "A" * 32
  "#{a}:" == "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA:"
end

##
# Struct ISO Test

if Object.const_defined?(:Struct)
  assert('Struct', '15.2.18') do
    Struct.class == Class
  end

  assert('Struct superclass', '15.2.18.2') do
    Struct.superclass == Object
  end

  assert('Struct.new', '15.2.18.3.1') do
    c = Struct.new(:m1, :m2)
    c.superclass == Struct and
      c.members == [:m1,:m2]
  end

  # Check crash bug with Struc.new and no params.
  assert('Struct.new', '15.2.18.3.1') do
     c = Struct.new()
     c.superclass == Struct and c.members == []
  end

  assert('Struct#==', '15.2.18.4.1') do
    c = Struct.new(:m1, :m2)
    cc1 = c.new(1,2)
    cc2 = c.new(1,2)
    cc1 == cc2
  end

  assert('Struct#[]', '15.2.18.4.2') do
    c = Struct.new(:m1, :m2)
    cc = c.new(1,2)
    cc[:m1] == 1 and cc["m2"] == 2
  end

  assert('Struct#[]=', '15.2.18.4.3') do
    c = Struct.new(:m1, :m2)
    cc = c.new(1,2)
    cc[:m1] = 3
    cc[:m1] == 3
  end

  assert('Struct#each', '15.2.18.4.4') do
    c = Struct.new(:m1, :m2)
    cc = c.new(1,2)
    a = []
    cc.each{|x|
      a << x
    }
    a[0] == 1 and a[1] == 2
  end

  assert('Struct#each_pair', '15.2.18.4.5') do
    c = Struct.new(:m1, :m2)
    cc = c.new(1,2)
    a = []
    cc.each_pair{|k,v|
      a << [k,v]
    }
    a[0] == [:m1, 1] and a[1] == [:m2, 2]
  end

  assert('Struct#members', '15.2.18.4.6') do
    c = Struct.new(:m1, :m2)
    cc = c.new(1,2)
    cc.members == [:m1,:m2]
  end

  assert('Struct#select', '15.2.18.4.7') do
    c = Struct.new(:m1, :m2)
    cc = c.new(1,2)
    cc.select{|v| v % 2 == 0} == [2]
  end
end

##
# Symbol ISO Test

assert('Symbol', '15.2.11') do
  Symbol.class == Class
end

assert('Symbol superclass', '15.2.11.2') do
  Symbol.superclass == Object
end

assert('Symbol#===', '15.2.11.3.1') do
  :abc === :abc and not :abc === :cba
end

assert('Symbol#id2name', '15.2.11.3.2') do
  :abc.id2name == 'abc'
end

assert('Symbol#to_s', '15.2.11.3.3') do
  :abc.to_s == 'abc'
end

assert('Symbol#to_sym', '15.2.11.3.4') do
  :abc.to_sym == :abc
end
assert('super', '11.3.4') do
  test = false
  begin
    super
  rescue NoMethodError
    test = true
  end

  class SuperFoo
    def foo
      true
    end
    def bar(*a)
      a
    end
  end
  class SuperBar < SuperFoo
    def foo
      super
    end
    def bar(*a)
      super(*a)
    end
  end
  bar = SuperBar.new
  test &&= bar.foo
  test &&= (bar.bar(1,2,3) == [1,2,3])
  test
end

assert('yield', '11.3.5') do
  begin
    yield
  rescue LocalJumpError
    true
  else
    false
  end
end

assert('Abbreviated variable assignment', '11.4.2.3.2') do
  a ||= 1
  b &&= 1
  c = 1
  c += 2
  a == 1 and b == nil and c == 3
end
##
# Time ISO Test

if Object.const_defined?(:Time)
  assert('Time.new', '15.2.3.3.3') do
    Time.new.class == Time
  end

  assert('Time', '15.2.19') do
    Time.class == Class
  end

  assert('Time superclass', '15.2.19.2') do
    Time.superclass == Object
  end

  assert('Time.at', '15.2.19.6.1') do
    Time.at(1300000000.0)
  end

  assert('Time.gm', '15.2.19.6.2') do
    Time.gm(2012, 12, 23)
  end

  assert('Time.local', '15.2.19.6.3') do
    Time.local(2012, 12, 23)
  end

  assert('Time.mktime', '15.2.19.6.4') do
    Time.mktime(2012, 12, 23)
  end

  assert('Time.now', '15.2.19.6.5') do
    Time.now.class == Time
  end

  assert('Time.utc', '15.2.19.6.6') do
    Time.utc(2012, 12, 23)
  end

  assert('Time#+', '15.2.19.7.1') do
    t1 = Time.at(1300000000.0)
    t2 = t1.+(60)

    t2.utc.asctime == "Sun Mar 13 07:07:40 UTC 2011"
  end

  assert('Time#-', '15.2.19.7.2') do
    t1 = Time.at(1300000000.0)
    t2 = t1.-(60)

    t2.utc.asctime == "Sun Mar 13 07:05:40 UTC 2011"
  end

  assert('Time#<=>', '15.2.19.7.3') do
    t1 = Time.at(1300000000.0)
    t2 = Time.at(1400000000.0)
    t3 = Time.at(1500000000.0)

    t2.<=>(t1) == 1 and
      t2.<=>(t2) == 0 and
      t2.<=>(t3) == -1 and
      t2.<=>(nil) == nil
  end

  assert('Time#asctime', '15.2.19.7.4') do
    Time.at(1300000000.0).utc.asctime == "Sun Mar 13 07:06:40 UTC 2011"
  end

  assert('Time#ctime', '15.2.19.7.5') do
    Time.at(1300000000.0).utc.ctime == "Sun Mar 13 07:06:40 UTC 2011"
  end

  assert('Time#day', '15.2.19.7.6') do
    Time.gm(2012, 12, 23).day == 23
  end

  assert('Time#dst?', '15.2.19.7.7') do
    not Time.gm(2012, 12, 23).utc.dst?
  end

  assert('Time#getgm', '15.2.19.7.8') do
    Time.at(1300000000.0).getgm.asctime == "Sun Mar 13 07:06:40 UTC 2011"
  end

  assert('Time#getlocal', '15.2.19.7.9') do
    t1 = Time.at(1300000000.0)
    t2 = Time.at(1300000000.0)
    t3 = t1.getlocal

    t1 == t3 and t3 == t2.getlocal
  end

  assert('Time#getutc', '15.2.19.7.10') do
    Time.at(1300000000.0).getutc.asctime == "Sun Mar 13 07:06:40 UTC 2011"
  end

  assert('Time#gmt?', '15.2.19.7.11') do
    Time.at(1300000000.0).utc.gmt?
  end

  # ATM not implemented
  # assert('Time#gmt_offset', '15.2.19.7.12') do

  assert('Time#gmtime', '15.2.19.7.13') do
    Time.at(1300000000.0).gmtime
  end

  # ATM not implemented
  # assert('Time#gmtoff', '15.2.19.7.14') do

  assert('Time#hour', '15.2.19.7.15') do
    Time.gm(2012, 12, 23, 7, 6).hour == 7
  end

  # ATM doesn't really work
  # assert('Time#initialize', '15.2.19.7.16') do

  assert('Time#initialize_copy', '15.2.19.7.17') do
    time_tmp_2 = Time.at(7.0e6)
    time_tmp_2.clone == time_tmp_2
  end

  assert('Time#localtime', '15.2.19.7.18') do
    t1 = Time.at(1300000000.0)
    t2 = Time.at(1300000000.0)

    t1.localtime
    t1 == t2.getlocal
  end

  assert('Time#mday', '15.2.19.7.19') do
    Time.gm(2012, 12, 23).mday == 23
  end

  assert('Time#min', '15.2.19.7.20') do
    Time.gm(2012, 12, 23, 7, 6).min == 6
  end

  assert('Time#mon', '15.2.19.7.21') do
    Time.gm(2012, 12, 23).mon == 12
  end

  assert('Time#month', '15.2.19.7.22') do
    Time.gm(2012, 12, 23).month == 12
  end

  assert('Times#sec', '15.2.19.7.23') do
    Time.gm(2012, 12, 23, 7, 6, 40).sec == 40
  end

  assert('Time#to_f', '15.2.19.7.24') do
    Time.at(1300000000.0).to_f == 1300000000.0
  end

  assert('Time#to_i', '15.2.19.7.25') do
    Time.at(1300000000.0).to_i == 1300000000
  end

  assert('Time#usec', '15.2.19.7.26') do
    Time.at(1300000000.0).usec == 0
  end

  assert('Time#utc', '15.2.19.7.27') do
    Time.at(1300000000.0).utc
  end

  assert('Time#utc?', '15.2.19.7.28') do
    Time.at(1300000000.0).utc.utc?
  end

  # ATM not implemented
  # assert('Time#utc_offset', '15.2.19.7.29') do

  assert('Time#wday', '15.2.19.7.30') do
    Time.gm(2012, 12, 23).wday == 0
  end

  assert('Time#yday', '15.2.19.7.31') do
    Time.gm(2012, 12, 23).yday == 357
  end

  assert('Time#year', '15.2.19.7.32') do
    Time.gm(2012, 12, 23).year == 2012
  end

  assert('Time#zone', '15.2.19.7.33') do
    Time.at(1300000000.0).utc.zone == 'UTC'
  end
end

##
# TrueClass ISO Test

assert('TrueClass', '15.2.5') do
  TrueClass.class == Class
end

assert('TrueClass superclass', '15.2.5.2') do
  TrueClass.superclass == Object
end

assert('TrueClass true', '15.2.5.1') do
  true
end

assert('TrueClass#&', '15.2.5.3.1') do
  true.&(true) and not true.&(false)
end

assert('TrueClass#^', '15.2.5.3.2') do
  not true.^(true) and true.^(false)
end

assert('TrueClass#to_s', '15.2.5.3.3') do
  true.to_s == 'true'
end

assert('TrueClass#|', '15.2.5.3.4') do
  true.|(true) and true.|(false)
end
##
# TypeError ISO Test

assert('TypeError', '15.2.29') do
  TypeError.class == Class
end

assert('TypeError superclass', '15.2.29.2') do
  TypeError.superclass == StandardError
end

