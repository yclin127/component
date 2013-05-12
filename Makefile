all:
	scons
clean:
	scons --clean

doxygen: doxygon.cfg
	doxygen doxygon.cfg
	make html -C doc

breathe:
	make html -C doc