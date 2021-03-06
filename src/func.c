/**
 * Print statistics on CTRL+C
 * @param int signum the number of the signal
 * @param siginfo_t *info the address to the siginfo_t structure
 * @param void *ptr null
 */
void sighandler(int signum, siginfo_t *info, void *ptr){
	printf("\nTime elapsed:\t%.0f seconds\n", difftime(time(NULL), start_sniff));
	printf("Total number of packet sniffed:\t%d\n", pktcount);
	printf("Total size of sniffed packets:\t%d Bytes\n", pkt_tot_size);
	exit(1);
}

/**
 * Print the help
 */
void help(){
	printf("Usage: %s -i <interface> -e <protocol> -m <mode> -v -c -n\n",APPNAME);
	printf("-i\tThe network interface used to listen.\n");
	printf("-e\tA filter to select which packets sniff (currently available: tcp, udp, ip, icmp, arp).\n\tExample: \"tcp && port 80\"\n");
	printf("-m\tOptional\tIf set to 0 enable promiscous mode (default), 1 to disable.\n");
	printf("-v\tOptional\tPrint more information about the packets.\n\t\t\tWorks only with some protocols.\n");
	printf("-c\tOptional\tControls if the IPv4 checksum is correct.\n");
	printf("-n\tOptional\tTry to resolve IP addresses to DNS names.\n");
	exit(1);
}

/**
 * Check the user
 */
void check_user(){
	if(getuid() != 0){
		printf("You must be root run in order to run %s.\n", APPNAME);
		exit(1);
	}
}


/**
 * Compute the checksum of ipv4
 * @param struct ip *iphdr the ip header from where compute checksum
 * @return 1 if the checksum are equals 0 if not
 */
int compute_checksum_ipv4(struct ip *iphdr){

	//merge version, header lenght and tos (differentiated services code point)	
	unsigned short firstShort;
	firstShort = (unsigned short)iphdr->ip_v << 12; 	//put ip_v to the right
	firstShort |= ((unsigned short)iphdr->ip_hl << 8);      //put ip_hl to next to ip_v
	firstShort |= ((unsigned short)iphdr->ip_tos);	        //put ip_tos into the last 8 bits

	//merge the ttl(int:8) and ip_p(int:8) into an unsigned short
	unsigned short secondShort;
	secondShort = iphdr->ip_ttl << 8;
	secondShort |= iphdr->ip_p;

	//split the src and destination ip from (int:32) to (short:16)
	unsigned short first_src_ip = (unsigned short)(htonl(iphdr->ip_src.s_addr) >> 16); //this conversion put the first 16 bit starting from the left to the right
	unsigned short snd_src_ip = (unsigned short)htonl(iphdr->ip_src.s_addr); //this conversion erases the last 16 bits in order to cast to short
	unsigned short first_dst_ip = (unsigned short)(htonl(iphdr->ip_dst.s_addr) >> 16);
	unsigned short snd_dst_ip = (unsigned short)htonl(iphdr->ip_dst.s_addr);

	//sum all the shorts of the ip header except the checum field
	unsigned int sum = firstShort + ntohs(iphdr->ip_len) + ntohs(iphdr->ip_id) + ntohs(iphdr->ip_off) + secondShort + first_src_ip + first_dst_ip + snd_src_ip + snd_dst_ip;

	//the first 4 bits from the left are the carry,
	//this conversion shit the first 16 bit from the right in order to obtain the last four bits
	unsigned int carry = sum >> 16;

	//get only the first 16 bit from the right
	unsigned int rest = sum&65535;

	//sum rest and carry and then invert the bits. 
	//This must be equal to the checksum of the ip header
	unsigned short results = ~(rest + carry);

	return (results == ntohs(iphdr->ip_sum));
}

/**
 * Resolve a given IP address to a name 
 * @param unsigned long addr The IP address in its binary form rapresentation 
 * @param char * hostname the variable that must be used to store the hostname. It must be initialized with malloc(MAX_HOST_NAME).
 * @return 1 in case of success, -1 in case of failure
 */
int resolve_address_to_name(unsigned long addr, char *hostname){
	struct sockaddr_in netparam;
	netparam.sin_family = AF_INET;
	netparam.sin_port = htons(0);
	netparam.sin_addr.s_addr = addr;

	if(getnameinfo((struct sockaddr *)&netparam, sizeof(netparam), hostname, MAX_HOST_NAME, NULL, sizeof(NULL), 0) == 0) return 0;
	else{
		perror("getnameinfo");
		return -1;
	}

	return 0;	
}


/**
 * Print the payload of packets 
 * @param const unsigned char *packet The sniffed packet
 */
void print_payload(const unsigned char *packet){
	int i;

	printf("--------- Payload --------\n");

	for(i = 0 ; i < strlen((char *)packet+54) ; i++){
		if(isprint(packet[i+54]))
			printf("%c", packet[i+54]);
		else
			printf(".");

		//after 60 character print new line
		if(i % 60 == 0 && i != 0) printf("\n\r");
	}


	printf("\n\n");
}


/**
 * Print the name of the DNS question section
 * @param const unsigned char *packet a given packet with DNS information
 * @param int index the beginning index of the name analysis
 * @param int *question_size the pointer where to store the length of the name
 */
void print_dns_name(const unsigned char *packet, int index, int *question_size){
	int i = 0, newindex = 0;
	int length, total_length;

	//printf("INDEX %d ===== %x\n", index, packet[index+1]);

	if(packet[index] == 0xc0) print_dns_name(packet,42+packet[index+1], question_size);

	length = packet[index];
	total_length = 1+length; //1 byte for the size indicator + the total number of chars
	*question_size += total_length;

	for(i = 1 ; i <= length ; i++)
		printf("%c", packet[index+i]);

	if(packet[index+length+1] == 0){
		printf("\n");
	     	*question_size += 1;
		return;
	}
	//to avoid endless call to the recursive function, we check if the name is longer than 255 octets
	else if(*question_size > 255){
		return;
	}
	else {
		printf(".");
		//printf("PACKET VALUE: %x\n", packet[index+total_length]);
		//printf("NEXT VALUE: %d\n", packet[index+total_length+1]);
		if(packet[index+total_length] == 0xc0) newindex = 42+packet[index+total_length+1];
		else newindex = index+total_length;

		//printf("NEWINDEX: %d", newindex);

		print_dns_name(packet, newindex, question_size);
	}
}

/**
 * Save in the specified array a binary reperesentation of num
 * @param unsigned char bit_string[] where to store the result
 * @param int array_size the size of the passed array
 * @param int num the number to be converted
 */
void itob(unsigned char bit_string[], int array_size, int num){
	int i;

	for(i = 0 ;  i < array_size ; i++){
		bit_string[i] = (num & (int)pow(2,array_size-1-i)) == 0 ? '0' : '1';
	}
}

