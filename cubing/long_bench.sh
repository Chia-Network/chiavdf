#!/bin/bash
make -B libq;

for i in {0..1}
do

	for j in {0..1}
	do
		if [ "$i" == 0 ]; then
			if [ "$j" == 0 ]; then
				printf "\n\ncubing never reducing\n\n"
			else
				printf "\n\ncubing always reducing\n\n"
			fi
		else
			if [ "$j" == 0 ] ; then
				printf "\n\nsquaring  never reducing\n\n"
			else
				printf "\n\nsquaring always reducing\n\n"
			fi
		fi

		if [ "$i" == 1 ]; then
	  		./libq 30000000 "$i" "$j";
		else
  			./libq 18927892 "$i" "$j";
		fi
		printf "\n"

	done
done
