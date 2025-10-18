# pcpacktool
this tool able to extract and reimport PCPACKS present on Ultimate Spider-Man (PC) game 

# Requirements

string_hash_dictionary.txt

# Extract mode


cmd line : pcpacktool.exe export NAME_EXAMPLE.PCPACK --dict string_hash_dictionary.txt --outdir NAME_EXAMPLE

# Reimport mode


cmd line : pcpacktool.exe import NAME_EXAMPLE.pcpack --dict string_hash_dictionary.txt --in  NAME_EXAMPLE --out NAME_EXAMPLE_.PCPACK


# Reimport mode alligned files


cmd line : pcpacktool.exe import NAME_EXAMPLE.pcpack --dict string_hash_dictionary.txt --in  NAME_EXAMPLE --out NAME_EXAMPLE_.PCPACK  --update-dir --payload-align 16
