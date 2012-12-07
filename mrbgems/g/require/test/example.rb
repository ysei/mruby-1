assert('require test') do
  Kernel.respond_to? :require
end
