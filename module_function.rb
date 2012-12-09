module Mod
  def self.one
    p "This is one"
  end
  #module_function :one
end
class Cls
  include Mod
  def callOne
    Mod.one
  end
end
Mod.one	
c = Cls.new
c.callOne	
p "here"
module Mod
  def self.one
    p "This is the new one"
  end
end
Mod.one	
c.callOne	

#private	 private( [ aSymbol ]* ) 
#With no arguments, sets the default visibility for subsequently defined methods to private. With arguments, sets the named methods to have private visibility. See Access Control starting on page 233.
module Mod
  def a()  end
  def b()  end
  #private
  def c()  end
  private :a
end
p "Here"
p Mod.private_instance_methods