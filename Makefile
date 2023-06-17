symtable/table.cpp: symtable/table.py
	python3 symtable/table.py > symtable/table.cpp

symtable/table.cpp.o: symtable/table.cpp
	python3 build.py symtable/table.cpp

.PHONY = clean

clean:
	rm -f symtable/table.cpp.o symtable/table.cpp.s symtable/table.cpp
