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

class Thing
end

a = "def hello()  \"Hello Thing!\" end"

Thing.module_eval(a)

p Thing.new.hello() == "Hello Thing!"

module Berlu
end
a = "def self.hello()  p \"Hello Berlu!\" end "
Berlu.module_eval(a)  
Berlu.hello()

