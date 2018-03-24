#include <iostream>
#include <ostream>
#include <string>
#include <vector>
#include <boost\date_time\local_time\local_time.hpp>
#include <boost\date_time\posix_time\posix_time.hpp>
#include <boost\asio.hpp>
#include <boost\bind.hpp>
#include <boost\asio\ssl.hpp>
#include <boost\algorithm\string\classification.hpp> 
#include <boost\algorithm\string\split.hpp>
#include <boost\algorithm\string\erase.hpp>
#include <pqxx/pqxx> 
#include <sstream>
#include "gumbo.h"
#include "csv.h"

using boost::asio::ip::tcp;

struct stockdata {
	std::string date;
	double open;
	double high;
	double low;
	double close;
	double adj_close;
	double volume;
};

struct wsjblkdata {
	std::string sMktType; 
	std::string sMktName;
	float sLast;
	float sChg; 
	float sPctChg; 
	float sMoneyFlow; 
	float sTickUp; 
	float sTickDown; 
	float sUpDownRatio; 
	float sBlkMoneyFlow; 
	float sBlkTickUp; 
	float sBlkTickDown; 
	float sBlkUpDownRatio; 
	std::string sDate;
};

// enum of WSJ Block Market types
enum wsjmarketdata_code {
	eDJIA,
	eTSM,
	eBasicMaterials,
	eConsumerGoods,
	eConsumerServices,
	eFinancials,
	eHealthCare,
	eIndustrials,
	eOilGas,
	eTechnology,
	eTelecommunications,
	eUtilities,
	eNone
};

// look WSJ Block Data market type
std::string wsjmarkettype(wsjmarketdata_code in_code) {
	switch (in_code) {
	case eDJIA:
		return "DJIA";
		break;
	case eTSM:
		return "TSM";
		break;
	case eBasicMaterials:
		return "BM";
		break;
	case eConsumerGoods:
		return "CG";
		break;
	case eConsumerServices:
		return "CS";
		break;
	case eFinancials:
		return "FIN";
		break;
	case eHealthCare:
		return "HC";
		break;
	case eIndustrials:
		return "IND";
		break;
	case eOilGas:
		return "OG";
		break;
	case eTechnology:
		return "TECH";
		break;
	case eTelecommunications:
		return "TELE";
		break;
	case eUtilities:
		return "UTIL";
		break;
	default:
		return "DK";
}

}
wsjmarketdata_code hashit(std::string const& inString) {
	if (inString == "Dow Jones Industrial Average")			return eDJIA;
	else if (inString == "DJ U.S. TSM (float)")				return eTSM;
	else if (inString == "Basic Materials")					return eBasicMaterials;
	else if (inString == "Consumer Goods")					return eConsumerGoods;
	else if (inString == "Consumer Services")				return eConsumerServices;
	else if (inString == "Financials")						return eFinancials;
	else if (inString == "Health Care")						return eHealthCare;
	else if (inString == "Industrials")						return eIndustrials;
	else if (inString == "Oil & Gas")						return eOilGas;
	else if (inString == "Technology")						return eTechnology;
	else if (inString == "Telecommunications")				return eTelecommunications;
	else if (inString == "Utilities")						return eUtilities;
	else return eNone;
}


class https_client
{
public:
	https_client(boost::asio::io_service& io_service,
		boost::asio::ssl::context& context,
		const std::string& server, const std::string& path)
		: resolver_(io_service),
		socket_(io_service, context)
	{

		// Form the request. We specify the "Connection: close" header so that the
		// server will close the socket after transmitting the response. This will
		// allow us to treat all data up until the EOF as the content.
		std::ostream request_stream(&request_);
		request_stream << "GET " << path << " HTTP/1.0\r\n";
		request_stream << "Host: " << server << "\r\n";
		request_stream << "Accept: */*\r\n";
		request_stream << "Connection: close\r\n\r\n";

		// Start an asynchronous resolve to translate the server and service names
		// into a list of endpoints.
		tcp::resolver::query query(server, "https");
		resolver_.async_resolve(query,
			boost::bind(&https_client::handle_resolve, this,
				boost::asio::placeholders::error,
				boost::asio::placeholders::iterator));
	}

	std::string my_response() {
		return strData_;
	}

private:

	void handle_resolve(const boost::system::error_code& err,
		tcp::resolver::iterator endpoint_iterator)
	{
		if (!err)
		{
			std::cout << "Resolve OK" << "\n";
			socket_.set_verify_mode(boost::asio::ssl::verify_peer);
			socket_.set_verify_callback(
				boost::bind(&https_client::verify_certificate, this, _1, _2));

			boost::asio::async_connect(socket_.lowest_layer(), endpoint_iterator,
				boost::bind(&https_client::handle_connect, this,
					boost::asio::placeholders::error));
		}
		else
		{
			std::cout << "Error resolve: " << err.message() << "\n";
		}
	}

	bool verify_certificate(bool preverified,
		boost::asio::ssl::verify_context& ctx)
	{
		// The verify callback can be used to check whether the certificate that is
		// being presented is valid for the peer. For example, RFC 2818 describes
		// the steps involved in doing this for HTTPS. Consult the OpenSSL
		// documentation for more details. Note that the callback is called once
		// for each certificate in the certificate chain, starting from the root
		// certificate authority.

		// In this example we will simply print the certificate's subject name.
		char subject_name[256];
		X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
		X509_NAME_oneline(X509_get_subject_name(cert), subject_name, 256);
		std::cout << "Verifying " << subject_name << "\n";

		//jst 2/19/18
		//return preverified;
		return true;
	}

	void handle_connect(const boost::system::error_code& err)
	{
		if (!err)
		{
			std::cout << "Connect OK " << "\n";
			socket_.async_handshake(boost::asio::ssl::stream_base::client,
				boost::bind(&https_client::handle_handshake, this,
					boost::asio::placeholders::error));
		}
		else
		{
			std::cout << "Connect failed: " << err.message() << "\n";
		}
	}

	void handle_handshake(const boost::system::error_code& error)
	{
		if (!error)
		{
			std::cout << "Handshake OK " << "\n";
			std::cout << "Request: " << "\n";
			const char* header = boost::asio::buffer_cast<const char*>(request_.data());
			std::cout << header << "\n";

			// The handshake was successful. Send the request.
			boost::asio::async_write(socket_, request_,
				boost::bind(&https_client::handle_write_request, this,
					boost::asio::placeholders::error));
		}
		else
		{
			std::cout << "Handshake failed: " << error.message() << "\n";
		}
	}

	void handle_write_request(const boost::system::error_code& err)
	{
		if (!err)
		{
			// Read the response status line. The response_ streambuf will
			// automatically grow to accommodate the entire line. The growth may be
			// limited by passing a maximum size to the streambuf constructor.
			boost::asio::async_read_until(socket_, response_, "\r\n",
				boost::bind(&https_client::handle_read_status_line, this,
					boost::asio::placeholders::error));
		}
		else
		{
			std::cout << "Error write req: " << err.message() << "\n";
		}
	}

	void handle_read_status_line(const boost::system::error_code& err)
	{
		if (!err)
		{
			// Check that response is OK.
			std::istream response_stream(&response_);
			std::string http_version;
			response_stream >> http_version;
			unsigned int status_code;
			response_stream >> status_code;
			std::string status_message;
			std::getline(response_stream, status_message);
			if (!response_stream || http_version.substr(0, 5) != "HTTP/")
			{
				std::cout << "Invalid response\n";
				return;
			}
			if (status_code != 200)
			{
				std::cout << "Response returned with status code ";
				std::cout << status_code << "\n";
				return;
			}
			std::cout << "Status code: " << status_code << "\n";

			// Read the response headers, which are terminated by a blank line.
			//boost::asio::async_read_until(socket_, response_, "\r\n\r\n",
			boost::asio::async_read_until(socket_, responseHeader_, "\r\n\r\n",
				boost::bind(&https_client::handle_read_headers, this,
					boost::asio::placeholders::error));
		}
		else
		{
			std::cout << "Error: " << err.message() << "\n";
		}
	}

	void handle_read_headers(const boost::system::error_code& err)
	{
		if (!err)
		{
			// Process the response headers. responseHeader_
			//			std::istream response_stream(&response_);
			std::istream response_stream(&responseHeader_);
			std::string header;
			while (std::getline(response_stream, header) && header != "\r")
				std::cout << header << "\n";
			std::cout << "\n";

			// Write whatever content we already have to output.
			if (response_.size() > 0) {
				//std::cout << &responseHeader_;
				std::stringstream ss;
				ss << &responseHeader_;
				strData_.append(ss.str());
			}
			//				std::cout << &response_;

			// Start reading remaining data until EOF.
			//boost::asio::async_read(socket_, response_,
			boost::asio::async_read(socket_, responseBody_,
				boost::asio::transfer_at_least(1),
				boost::bind(&https_client::handle_read_content, this,
					boost::asio::placeholders::error));
		}
		else
		{
			std::cout << "Error: " << err << "\n";
		}
	}

	void handle_read_content(const boost::system::error_code& err)
	{
		if (!err)
		{
			// Write all of the data that has been read so far.
			//std::cout << &responseBody_;

			std::stringstream ss;
			ss << &responseBody_;
			strData_.append(ss.str());

			// Continue reading remaining data until EOF.
			boost::asio::async_read(socket_, responseBody_,
				boost::asio::transfer_at_least(1),
				boost::bind(&https_client::handle_read_content, this,
					boost::asio::placeholders::error));
		}
		else if (err != boost::asio::error::eof)
		{
			std::cout << "Error: " << err << "\n";
		}
	}

	tcp::resolver resolver_;
	boost::asio::ssl::stream<boost::asio::ip::tcp::socket> socket_;
	boost::asio::streambuf request_;
	boost::asio::streambuf response_;
	boost::asio::streambuf responseHeader_;
	boost::asio::streambuf responseBody_;
	std::string strData_;
};

/*
Get current date in yyyymmdd format
*/
static const std::string getCurrentDate() {

	namespace pt = boost::posix_time;

	pt::ptime now = pt::second_clock::local_time();

	std::string strTemp = boost::posix_time::to_iso_string(now); //yyyymmddTttttttt

																 // change to substr first 8!!
																 //std::stringstream ss;
																 //ss << strTemp.substr(0, 4) << strTemp.substr(4, 2) << strTemp.substr(6, 2);

	return strTemp.substr(0, 8);

	//return ss.str();
}

/*
Date of last SPY quote
*/
static int getMaxSPYDate(const pqxx::connection* in_pConn, std::string& out_date) {

	using namespace std;
	using namespace pqxx;

	/* Create SQL statement */
	char* sql = "SELECT to_char(max(date),'YYYYMMDD') from public.stock_quotes where symbol='SPY' ";

	out_date = "";

	try {
		// check if connection is good
		if (in_pConn != NULL && !in_pConn->is_open()) {
			cout << "database not open" << endl;
			return -1;
		}
		/* Create a non-transactional object. */
		nontransaction N((pqxx::connection&)*in_pConn); // not sure why the cast?? but it works

														/* Execute SQL query */
		result R(N.exec(sql));

		/* List down all the records */
		for (result::const_iterator c = R.begin(); c != R.end(); ++c) {
			out_date = c[0].as<string>();
			cout << "date = " << out_date << endl;
		}
		cout << "Operation done successfully" << endl;

	}
	catch (const std::exception &e) {
		cerr << e.what() << std::endl;
		return -2;
	}

	return 0;
}

/*
Get Postgresql connection
*/
pqxx::connection* getDBConnection() {

	using namespace std;
	using namespace pqxx;

	connection* pConn = NULL;
	//
	try {
		pConn = new connection("dbname = postgres user = postgres password = admin \
						hostaddr = 127.0.0.1 port = 5432");
	}
	catch (const std::exception &e) {
		cerr << e.what() << std::endl;
	}

	return pConn;
}

/*
**************** Gumbo html parsing **********
*/
static void processtable(GumboNode* node, std::vector<std::string>& vecOutData, std::string& sTRData) {

	//current node is table
	// loop thru children
	// if TR
	//   then parse data
	//   write record
	// repeat

	if (node->v.element.tag == GUMBO_TAG_TR) {	// check for Row (TR) element w/in table
		std::cout << std::endl;
		if (!sTRData.empty()) {
			vecOutData.push_back(sTRData);	// adds a copy
			sTRData = "";					//reset TR data
		}
	}

	if (node->type == GUMBO_NODE_TEXT) {
		std::cout << node->v.text.text << "|";
		sTRData.append(node->v.text.text);
		sTRData.append("|");
	}
	else if (node->type == GUMBO_NODE_ELEMENT &&
		node->v.element.tag != GUMBO_TAG_SCRIPT &&
		node->v.element.tag != GUMBO_TAG_STYLE) {

		GumboVector* children = &node->v.element.children;
		for (unsigned int i = 0; i < children->length; ++i) {
			processtable((GumboNode*)children->data[i], vecOutData, sTRData);
		}
		return;
	}
	else {
		return;
	}
}

/*
Parse html looking for data table "mdcTable"
Once found, parse table
*/
static void search_for_table(GumboNode* node, std::vector<std::string>& vecOutData) {

	if (node->type != GUMBO_NODE_ELEMENT) {
		return;
	}

	// find data table 'mdcTable'
	GumboAttribute* aClass;
	if (node->v.element.tag == GUMBO_TAG_TABLE &&	// check for table html tag 
		(aClass = gumbo_get_attribute(&node->v.element.attributes, "class"))) { // check class for 'mdcTable'
																				
		if (strcmp(aClass->value, "mdcTable") == 0) {    // if table contains market data
			std::cout << "\n**************** mdcTable *************\n" << std::endl;
			std::string sData = "";

			GumboVector* children = &node->v.element.children;						// get children
			for (unsigned int i = 0; i < children->length; ++i) {					// loop thru
				GumboNode* pNode = static_cast<GumboNode*>(children->data[i]);		// cast each child
				processtable((GumboNode*)children->data[i], vecOutData, sData);
				std::cout << std::endl;
			}
			if (sData.length() > 0)			//get last record if exists
				vecOutData.push_back(sData);

			std::cout << "\n***************************************\n" << std::endl;

			return; // exit routine
		}
	}

	GumboVector* children = &node->v.element.children;
	for (unsigned int i = 0; i < children->length; ++i) {
		search_for_table(static_cast<GumboNode*>(children->data[i]), vecOutData); // recursive call to search_for_table
	}
}

static int get_page(const std::string& in_domain, const std::string& in_path, std::string& out_page) {

	std::cout << "************** START GET PAGE *******************\n" << std::endl;

	boost::asio::ip::tcp::iostream s;

	// The entire sequence of I/O operations must complete within 60 seconds.
	// If an expiry occurs, the socket is automatically closed and the stream
	// becomes bad.
	s.expires_from_now(boost::posix_time::seconds(60));

	// Establish a connection to the server.
	s.connect(in_domain, "http");
	if (!s)
	{
		std::cout << "Unable to connect: " << s.error().message() << "\n";
		return 1;
	}

	// Send the request. We specify the "Connection: close" header so that the
	// server will close the socket after transmitting the response. This will
	// allow us to treat all data up until the EOF as the content.
	s << "GET " << in_path << " HTTP/1.0\r\n";
	s << "Host: " << in_domain << "\r\n";
	s << "Accept: */*\r\n";
	s << "Connection: close\r\n\r\n";

	// By default, the stream is tied with itself. This means that the stream
	// automatically flush the buffered output before attempting a read. It is
	// not necessary not explicitly flush the stream at this point.

	// Check that response is OK.
	std::string http_version;
	s >> http_version;
	unsigned int status_code;
	s >> status_code;
	std::string status_message;
	std::getline(s, status_message);
	if (!s || http_version.substr(0, 5) != "HTTP/")
	{
		std::cout << "Invalid response\n";
		std::cout << "Socket status code: " << status_code << "\n";
		std::cout << "Status message: " << status_message << "\n";
		return 1;
	}
	if (status_code != 200)
	{
		std::cout << "Response returned with status code " << status_code << "\n";
		return 1;
	}

	// Process the response headers, which are terminated by a blank line.
	std::string header;
	while (std::getline(s, header) && header != "\r")
		std::cout << header << "\n";
	std::cout << "\n";

	// tcp instream data to stringstream
	std::stringstream ssPage;
	ssPage << s.rdbuf();

	// set return variable to page data
	//out_page = std::string(ssPage.str());
	out_page = ssPage.str();

	return 0;
}

/*
date validation functions
*/
struct dateparser
{
	dateparser(std::string fmt)
	{
		// set format
		using namespace boost::local_time;
		local_time_input_facet* input_facet = new local_time_input_facet();
		input_facet->format(fmt.c_str());
		ss.imbue(std::locale(ss.getloc(), input_facet));
	}

	bool operator()(std::string const& text)
	{
		ss.clear();
		ss.str(text);

		ss >> pt;

		//bool ok = ss >> pt;

		if (!pt.is_not_a_date_time())
		{
			auto tm = to_tm(pt);
			year = tm.tm_year;
			month = tm.tm_mon + 1; // for 1-based (1:jan, .. 12:dec)
			day = tm.tm_mday;
		}
		else return false;

		return true;
	}

	boost::posix_time::ptime pt;
	unsigned year, month, day;

private:
	std::stringstream ss;
};

/*
check start date is numeric
*/
bool is_number(const std::string& s)
{
	return !s.empty() && std::find_if(s.begin(),
		s.end(), [](char c) { return !std::isdigit(c); }) == s.end();
}

static int chk_run_date(const std::string& in_run_date) {

	//int iRC = 0;

	if (in_run_date.length() < 8)
		return -1;
	else if (!is_number(in_run_date))
	{
		return -2;
	}

	// check for valid date using struct
	dateparser parser("%Y%m%d"); // not thread safe
	if (parser(in_run_date))
		std::cout << in_run_date << " -> " << parser.pt << " is the "
		<< parser.day << "th of "
		<< std::setw(2) << std::setfill('0') << parser.month
		<< " in the year " << parser.year << "\n";
	else {
		std::cout << in_run_date << " is not a valid date\n";
		return -3;
	}

	return 0;
}

int getMissingSPYData(int in_spy_date, std::vector<stockdata*>& out_vecStockData) {

	int iRC = 0;

	/******* get 1yr stock data in csv format  ************/
	//https://api.iextrading.com/1.0/stock/spy/chart/1y?format=json
	std::string strReturnPage("");
	std::string strDomain("api.iextrading.com");
	std::string strPath("/1.0/stock/spy/chart/1y?format=csv");

	boost::asio::ssl::context ctx(boost::asio::ssl::context::sslv23);
	ctx.set_verify_mode(boost::asio::ssl::verify_none);
	//ctx.set_default_verify_paths();

	boost::asio::io_service io_service;
	https_client c(io_service, ctx, strDomain, strPath);
	io_service.run();
	std::string myResp(c.my_response());
	std::istringstream issResp(myResp);
	/************************************************************/

	
	io::CSVReader<12, io::trim_chars<' '>, io::double_quote_escape<',', '\"'> > in("myFile", issResp);
	in.read_header(io::ignore_extra_column, "date", "open", "high", "low", "close", "volume", "unadjustedVolume", "change", "changePercent", "vwap", "label", "changeOverTime");

	std::string date; double open; double high; double low; double close; long volume; long unadjVolume; double change; double changePercent; double vwap; std::string label; double chgOverTime;

	while (in.read_row(date, open, high, low, close, volume, unadjVolume, change, changePercent, vwap, label, chgOverTime)) {
		std::cout << date << "," << close << std::endl;

		std::stringstream ss;
		ss << date.substr(0, 4) << date.substr(5, 2) << date.substr(8, 2);
		int iIEXSPYDate = std::stoi(ss.str());

		// if date > last spy date then
		if (iIEXSPYDate > in_spy_date) {

			stockdata* pStockData = new stockdata;

			pStockData->date = date;
			pStockData->open = open;
			pStockData->high = high;
			pStockData->low = low;
			pStockData->close = close;
			pStockData->adj_close = close;
			pStockData->volume = volume;

			out_vecStockData.push_back(pStockData);

		}  //endif

	}  //endwhile

	return iRC;
}

int processStockData(pqxx::connection* in_pConn) {

	int iRC = 0;

	std::vector<stockdata*> vecStockData; //vector to hold new SPY data

	std::string strCurrSPYDate;
	iRC = getMaxSPYDate(in_pConn, strCurrSPYDate); // last spy date
	int iCurrSPYDate = std::stoi(strCurrSPYDate);  // -- convert to integer yyymmdd 20170101

	//get stock quotes, pass in last SPY date + empty vector
	iRC = getMissingSPYData(iCurrSPYDate, vecStockData);

	//-- loop thru vector,insert into database, delete vector data struct (heap allocated)
	for (std::vector<stockdata*>::iterator it = vecStockData.begin(); it != vecStockData.end(); ++it) {
		stockdata* pStockData = (stockdata*)*it; //dereference iterator to stockdata struct 
		std::cout << "date: " << pStockData->date << std::endl;

		// insert into database
		using namespace std;
		using namespace pqxx;

		/* Create SQL statement */
		std::stringstream ss;

		ss << "INSERT INTO public.stock_quotes (symbol, date, open, high, close_x, low, adj_close, volume) VALUES ('SPY',";
		ss << "'" << pStockData->date << "',";
		ss << pStockData->open		<< ",";
		ss << pStockData->high		<< ",";
		ss << pStockData->close		<< ",";
		ss << pStockData->low		<< ",";
		ss << pStockData->adj_close << ",";
		ss << pStockData->volume	<< ")";

		std::string tempSQL = ss.str();

		const char* sql = tempSQL.c_str();

		try {
			// check if connection is good
			if (in_pConn != NULL && !in_pConn->is_open()) {
				cout << "database not open" << endl;
				return -1;
			}
			/* Create a non-transactional object. */
			pqxx::work txn((pqxx::connection&)*in_pConn);

			txn.exec(sql); /* Execute SQL insert */
			txn.commit();

			cout << "Operation done successfully" << endl;

		}
		catch (const std::exception &e) {
			cerr << e.what() << std::endl;

			//delete sql;
			delete pStockData; //delete struct memory

			return -2;
		}

		//delete sql;
		delete pStockData; //delete struct memory
	}

	return 0;
}

/*
	WSJ Market Data - get SPY dates from last WSJ Date processed
*/
int getSPYDates(pqxx::connection* in_pConn, const std::string& lastWSJDate, std::vector<std::string>& outVecDates) {

	using namespace std;
	using namespace pqxx;

	/* Create SQL statement */
	std::stringstream ss;

	ss << "SELECT to_char(date,'YYYYMMDD') from public.stock_quotes where symbol='SPY' and date > '";
	ss << lastWSJDate << "'";

	std::string tempSQL = ss.str();
	const char* sql = tempSQL.c_str();

	std::string out_date = "";

	try {
		// check if connection is good
		if (in_pConn != NULL && !in_pConn->is_open()) {
			cout << "database not open" << endl;
			return -1;
		}
		/* Create a non-transactional object. */
		nontransaction N((pqxx::connection&)*in_pConn); // not sure why the cast?? but it works
		result R(N.exec(sql)); /* Execute SQL query */

		/* List down all the records */
		for (result::const_iterator c = R.begin(); c != R.end(); ++c) {
			out_date = c[0].as<string>();
			outVecDates.push_back(out_date);
			cout << "date = " << out_date << endl;
		}
		cout << "getSPYDates Operation done successfully" << endl;

	}
	catch (const std::exception &e) {
		cerr << e.what() << std::endl;
		return -2;
	}

	return 0;
}

/*
	WSJ Market Date - Date of last WSJ Market Block quote
*/
static int getMaxWSJMarketDate(const pqxx::connection* in_pConn, std::string& out_date) {

	using namespace std;
	using namespace pqxx;

	/* Create SQL statement */
	char* sql = "SELECT to_char(max(date),'YYYYMMDD') from public.wsj_market_data where type='TSM' ";

	out_date = "";

	try {
		// check if connection is good
		if (in_pConn != NULL && !in_pConn->is_open()) {
			cout << "database not open" << endl;
			return -1;
		}
		/* Create a non-transactional object. */
		nontransaction N((pqxx::connection&)*in_pConn); // not sure why the cast?? but it works

														/* Execute SQL query */
		result R(N.exec(sql));

		/* List down all the records */
		for (result::const_iterator c = R.begin(); c != R.end(); ++c) {
			out_date = c[0].as<string>();
			cout << "date = " << out_date << endl;
		}
		cout << "Operation done successfully" << endl;

	}
	catch (const std::exception &e) {
		cerr << e.what() << std::endl;
		return -2;
	}

	return 0;
}

/*
	WSJ Market Data -  get WSJ Market Block data from web page
*/
int getWSJMarketData(const std::string& in_date, std::vector<std::string>& outVecData) {

	int iRC = 0;

	std::cout << "****************** START HTML PARSING **************\n" << std::endl;
	std::string sDomain = "www.wsj.com";
	std::string sPath = "";
	std::stringstream ssPath;
	
	ssPath << "/mdc/public/page/2_3022-mfsctrscan-moneyflow-" << in_date << ".html";
	sPath = ssPath.str();

	std::string sPage = "";

	iRC = get_page(sDomain, sPath, sPage);

	// if error getting WSJ Market data
	if (iRC != 0) {
		std::cout << "Error with get_page:\n";
		std::cout << "Domain: " << sDomain << "\n";
		std::cout << "Path: " << sPath << "\n" << std::endl;

		exit(-1); // exit program
	}

	//std::vector<std::string> vecData; // data from page table

	GumboOutput* output = gumbo_parse(sPage.c_str());
	search_for_table(output->root, outVecData);

	std::cout << "\nDestroying gumbo.............\n" << std::endl;
	gumbo_destroy_output(&kGumboDefaultOptions, output);

	std::cout << "****************** HTML PARSING FINISHED **************\n" << std::endl;

	return 0;
}
void cleanWSJData(std::string& in_string) {

	boost::erase_all(in_string, "$"); //remove all $
	boost::erase_all(in_string, ","); //remove all commas

	return ;
}

int parseWSJMarketData( std::vector<std::string>& const inVecData , std::vector<wsjblkdata*>& outWSJBlkStructData ) {
	
	// for each line of WSJ Market Data, split on '|', check first word, discard or process
	for (std::vector<std::string>::iterator it = inVecData.begin(); it != inVecData.end(); ++it) {

		std::string sTempRow = (std::string)*it;
		std::vector<std::string> words;
		wsjmarketdata_code mktCode;

		cleanWSJData(sTempRow); // remove $ and , 

		boost::split(words, sTempRow, boost::is_any_of("|"), boost::token_compress_on);

		mktCode = hashit(words[0]);

		if (mktCode != eNone) {
			// MktType|Last|Chg|% Chg|Money Flow|Tick Up|Tick Down|Up/Down Ratio|Money Flow|Tick Up|Tick Down|Up/Down Ratio|
			// "Dow Jones Industrial Average|20943.11|-32.67|-0.16|$85.12|$4,386.72|$4,301.60|1.02|$93.53|$1,287.79|$1,194.25|1.08|"
			// -- remove $ and commas
			struct wsjblkdata* wsjData	= new wsjblkdata;
			wsjData->sMktType			= wsjmarkettype(mktCode);
			wsjData->sMktName			= words[0];
			wsjData->sLast				= std::stof(words[1]);
			wsjData->sChg				= std::stof(words[2]);
			wsjData->sPctChg			= std::stof(words[3]);
			wsjData->sMoneyFlow			= std::stof(words[4]);
			wsjData->sTickUp			= std::stof(words[5]);
			wsjData->sTickDown			= std::stof(words[6]);
			wsjData->sUpDownRatio		= std::stof(words[7]);
			wsjData->sBlkMoneyFlow		= std::stof(words[8]);
			wsjData->sBlkTickUp			= std::stof(words[9]);
			wsjData->sBlkTickDown		= std::stof(words[10]);
			wsjData->sBlkUpDownRatio	= std::stof(words[11]);

			outWSJBlkStructData.push_back(wsjData);
	
		}

		if (words[0] == "WEEK") 
			break;  // exit for loop if starting weekly data
	}


	return 0;
}

int insertWSJBlkMktData(pqxx::connection* in_pConn, wsjblkdata* p_in_WSJBlkData, const std::string& in_date) {

	int iRC = 0;
	// insert into database
	using namespace std;
	using namespace pqxx;

		/* Create SQL statement */
	std::stringstream ss;

	ss << "INSERT INTO public.wsj_market_data (type, name, last, chg, pct_chg, total_mf, total_up, total_dn, total_ratio_up_dn, block_mf, block_up, block_dn, block_ratio_up_dn, date ) VALUES (";
		ss << "'" << p_in_WSJBlkData->sMktType << "',";
		ss << "'" << p_in_WSJBlkData->sMktName << "',";
		ss << p_in_WSJBlkData->sLast << ",";
		ss << p_in_WSJBlkData->sChg << ",";
		ss << p_in_WSJBlkData->sPctChg << ",";
		ss << p_in_WSJBlkData->sMoneyFlow << ",";
		ss << p_in_WSJBlkData->sTickUp << ",";
		ss << p_in_WSJBlkData->sTickDown << ",";
		ss << p_in_WSJBlkData->sUpDownRatio << ",";
		ss << p_in_WSJBlkData->sBlkMoneyFlow << ",";
		ss << p_in_WSJBlkData->sBlkTickUp << ",";
		ss << p_in_WSJBlkData->sBlkTickDown << ",";
		ss << p_in_WSJBlkData->sBlkUpDownRatio << ",";
		ss << "'" << in_date << "')";

		std::string tempSQL = ss.str();

		const char* sql = tempSQL.c_str();

		try {
			// check if connection is good
			if (in_pConn != NULL && !in_pConn->is_open()) {
				cout << "database not open" << endl;
				return -1;
			}
			/* Create a non-transactional object. */
			pqxx::work txn((pqxx::connection&)*in_pConn);

			txn.exec(sql); /* Execute SQL insert */
			txn.commit();

			cout << "Operation done successfully" << endl;

		}
		catch (const std::exception &e) {
			cerr << e.what() << std::endl;

			return -2;
		}

	return 0;
}

int processWSJMarketData(pqxx::connection* in_pConn) {

	int iRC = 0;
	std::string lastWSJDate = "";
	std::vector<std::string> outVecDates;
	std::vector<std::string> outWSJBlkRawData;
	std::vector<wsjblkdata*> outWSJBlkStructData;

	// get WSJ Max Market Date, place in lastWSJDate
	iRC = getMaxWSJMarketDate(in_pConn, lastWSJDate);

	// get SPY dates from last WSJ Market date (SPY dates = stk market calendar dates)
	iRC = getSPYDates(in_pConn, lastWSJDate, outVecDates);

	//for each SPY date in outVecDates, get WSJBlkMktData, parse data, insert into database 
	for (std::vector<std::string>::iterator it = outVecDates.begin(); it != outVecDates.end(); ++it) {
		std::cout << ' ' << *it;

		iRC = getWSJMarketData((std::string)*it, outWSJBlkRawData);		 // for SPY date, get rows of WSJBlkMktData (raw)
		iRC = parseWSJMarketData(outWSJBlkRawData, outWSJBlkStructData); // pass in vector of string data, clean it, get back structured typed data

		for (std::vector<wsjblkdata*>::iterator it2 = outWSJBlkStructData.begin(); it2 != outWSJBlkStructData.end(); ++it2) {

			insertWSJBlkMktData(in_pConn, (wsjblkdata*)*it2, (std::string)*it);

			delete (wsjblkdata*)*it2; //delete memory
		}

		outWSJBlkRawData.clear();		//clear vector of wsj page raw data
		outWSJBlkStructData.clear();	//clear vector of wsj page struct data

	}


	// ----- insert daily data into database

	return 0;
}

int main()
{
	int iRC = 0;

	try {
		pqxx::connection* pConn = getDBConnection();

		iRC = processStockData(pConn);
		iRC = processWSJMarketData(pConn);
		
		// clean up database connection
		pConn->disconnect();
		delete pConn;
	}
	catch (const std::exception &e) {
		std::cerr << e.what() << std::endl;
	}



	return 0;
}