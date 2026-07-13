
all: doxygen

doxygen:
	doxygen Doxyfile
	# Fix 700 and 500 permissions that Doxygen generates for some reason
	find html -type d -exec chmod 755 {} \;
	find html -type f -exec chmod 644 {} \;

run: doxygen
	http-server html

upload: doxygen
	rsync html/ vcvrack.com:docs-v2/ -ruz --info=progress2 --delete

clean:
	rm -rfv html
