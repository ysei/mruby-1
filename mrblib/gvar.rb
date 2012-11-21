class GlobalVars
    @@var = {}
    def get a
        return @@var[a]
    end
    
    def set a, b
        @@var[a] = b
    end
end

u = GlobalVars.new
u.set "path","./"
