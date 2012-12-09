 def mode_mask(mode, path)  #:nodoc:
    mask = 0
    mode.each_byte do |byte_chr|
      case byte_chr.chr
        when "r"
          mask |= 0444
        when "w"
          mask |= 0222
        when "x"
          mask |= 0111
        when "X"
          mask |= 0111 if FileTest::directory? path
        when "s"
          mask |= 06000
        when "t"
          mask |= 01000
      end
    end
    mask
  end
  SYSCASE = "aaa"
  DIRECTORY_TERM = "bbb"
  def descendant_diretory?(descendant, ascendant)
    #SYSCASE = "aaa"
    #DIRECTORY_TERM = "bbb"
    urlu = "\A(?#{SYSCASE}:#{Regexp.quote(ascendant)})#{DIRECTORY_TERM}"
    p urlu
    p descendant
    descendant  =~ /urlu/
  end


p descendant_diretory? "aaa:cccbbb", "..."

urlu = 1
class Thing
end
a = "def hello()  p \"Hello Thing! #{urlu}\" end "
#class Thing 
#  def hello() 
#    urlu = 1 
#    p "Hello there!" 
#  end 
#end
Thing.module_eval(a)  
p "back from eval"

Thing.new.hello()

module Berlu
  
end
a = "def self.hello()  p \"Hello Berlu! #{urlu}\" end "
Berlu.module_eval(a)  
Berlu.hello()

