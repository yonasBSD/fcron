#!/usr/bin/perl

# - read define assignations in config.h
# - read variable assignations in Makefile
# - construct a hash with that reads
# - read file given in arg 1 and perform substitutions
#   of the name of a variable by its value
#
# ex :  "@@VERSION@"   will be substitute by    "0.8.4"
#
#  + substitute "@@Date@" by the current date


%map = ();



open(CONFIG, "./config.h") or print "error while opening config.h\n" and exit;

while ( <CONFIG> ) {
    if ( /^\#define\s+(\w+?)\s+([\w\/-]+?)\s/ ) {
	$map{$1} = $2;
    }
    if ( /^\#define\s+(\w+?)\s+["](.+?)["]\s/ ) {	
	$map{$1} = $2;
    }
    
}

open(MAKEFILE, "./Makefile") or print "error while opening Makefile\n" and exit;

while ( <MAKEFILE> ) {
    if ( /^\s*?(\w+?)\s*?=\s*?([\w\.\/-]+)\s/ ) {
	$map{$1} = $2;
    }
    if ( /^\#define\s+(\w+?)\s+["](.+)["]\s/ ) {
	$map{$1} = $2;
    }
    
}

chop ($map{Date} = `date +%m/%d/%Y`);

open(SRC, $ARGV[0]) or print "error while opening $ARGV[0]\n" and exit;
open(DEST, ">$ARGV[1]") or print "error while opening $ARGV[1]\n" and exit;

while ( <SRC> ) {
    s/@@([^@]*)@/$map{$1}/g;
    print DEST $_;

}
