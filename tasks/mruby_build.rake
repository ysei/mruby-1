load 'tasks/mruby_build_gem.rake'
load 'tasks/mruby_build_commands.rake'

module MRuby
  class << self
    def targets
      @targets ||= {}
    end

    def each_target(&block)
      @targets.each do |key, target|
        target.instance_eval(&block)
      end
    end
  end

  class Toolchain
    class << self
      attr_accessor :toolchains
    end

    def initialize(name, &block)
      @name, @initializer = name.to_s, block
      MRuby::Toolchain.toolchains ||= {}
      MRuby::Toolchain.toolchains[@name] = self
    end

    def setup(conf)
      conf.instance_eval(&@initializer)
    end

    def toolchain(name)
      @@toolchains[name.to_s].setup(self)
    end

    def self.load
      Dir.glob("#{File.dirname(__FILE__)}/toolchains/*.rake").each do |file|
        Kernel.load file
      end
    end
  end
  Toolchain.load

  class Build
    class << self
      attr_accessor :current
    end
    include Rake::DSL
    include LoadGems
    attr_accessor :name, :bins, :exts, :file_separator
    attr_reader :root, :libmruby, :gems

    COMPILERS = %w(cc cxx objc asm)
    COMMANDS = COMPILERS + %w(linker archiver yacc gperf git exts mrbc)
    attr_block MRuby::Build::COMMANDS

    Exts = Struct.new(:object, :executable, :library)

    def initialize(name='host', &block)
      MRuby::Build.current = self
      @name = name
      @root = File.expand_path("#{File.dirname(__FILE__)}/..")

      if ENV['OS'] == 'Windows_NT'
        @exts = Exts.new('.o', '.exe', '.a')
        @file_separator = '\\'
      else
        @exts = Exts.new('.o', '', '.a')
        @file_separator = '/'
      end

      @cc = Command::Compiler.new(self, %w(.c))
      @cxx = Command::Compiler.new(self, %w(.cc .cxx .cpp))
      @objc = Command::Compiler.new(self, %w(.m))
      @asm = Command::Compiler.new(self, %w(.S .asm))
      @linker = Command::Linker.new(self)
      @archiver = Command::Archiver.new(self)
      @yacc = Command::Yacc.new(self)
      @gperf = Command::Gperf.new(self)
      @git = Command::Git.new(self)
      @mrbc = Command::Mrbc.new(self)

      @bins = %w(mruby mrbc mirb)
      @gems, @libmruby = [], []

      MRuby.targets[name.to_s] = self

      instance_eval(&block)

      compilers.each do |compiler|
        compiler.defines -= %w(DISABLE_GEMS) if respond_to?(:enable_gems?) && enable_gems?
        compiler.define_rules build_dir
      end
    end

    def toolchain(name)
      Toolchain.toolchains[name.to_s].setup(self)
    end

    def build_dir
      "build/#{self.name}"
    end

    def mrbcfile
      exefile("build/host/bin/mrbc")
    end

    def compilers
      COMPILERS.map do |c|
        instance_variable_get("@#{c}")
      end
    end

    def filename(name)
      if name.is_a?(Array)
        name.flatten.map { |n| filename(n) }
      else
        '"%s"' % name.gsub('/', file_separator)
      end
    end

    def exefile(name)
      if name.is_a?(Array)
        name.flatten.map { |n| exefile(n) }
      else
        "#{name}#{exts.executable}"
      end
    end

    def objfile(name)
      if name.is_a?(Array)
        name.flatten.map { |n| objfile(n) }
      else
        "#{name}#{exts.object}"
      end
    end

    def libfile(name)
      if name.is_a?(Array)
        name.flatten.map { |n| libfile(n) }
      else
        "#{name}#{exts.library}"
      end
    end

    def run_test
      puts ">>> Test #{name} <<<"
      mrbtest = exefile("#{build_dir}/test/mrbtest")
      sh "#{filename mrbtest}"
      puts 
    end
  end # Build

  class CrossBuild < Build
    def run_test
      mrbtest = exefile("#{build_dir}/test/mrbtest")
      puts "You should run #{mrbtest} on target device."
      puts 
    end
  end # CrossBuild
end # MRuby
