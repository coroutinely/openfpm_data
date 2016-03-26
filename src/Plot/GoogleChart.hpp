/*
 * GoogleChart.hpp
 *
 *  Created on: Jan 9, 2016
 *      Author: i-bird
 */

#ifndef OPENFPM_DATA_SRC_PLOT_GOOGLECHART_HPP_
#define OPENFPM_DATA_SRC_PLOT_GOOGLECHART_HPP_

#include <fstream>
#include "Vector/map_vector.hpp"

#define GGRAPH_COLUMS 1
#define GGRAPH_POINTS 2

/*! \brief Google chart options
 *
 */
struct GCoptions
{
	//! Title of the chart
	std::string title;
	//! Y axis name
	std::string yAxis;
	//! X axis name
	std::string xAxis;

	//! Type of chart (list of the option can be founded in Google Chart API for seriesType)
	//! Possible options are:
	//!  'line', 'area', 'bars', 'candlesticks', and 'steppedArea'
	//! default: line
	std::string stype;

	//! Extended series options
	//! Example {5: {type: 'line'}} specify that the series number 5 must be represented
	//! with a line
	std::string stypeext;

	size_t width=900;
	size_t heigh=500;

	//! Flag that specify if the colums are stacked
	//! Check in Google Chart for is stacked option
	bool isStacked = false;

	//! Width of the line
	size_t lineWidth = 4;

	//! Style for all the intervals
	//! Check Google Chart API intervals option
	std::string intervalsext;

	//! Style for each interval
	//! Check Google Chart API interval option
	std::string intervalext;

	//! more
	std::string more;

	GCoptions & operator=(const GCoptions & opt)
	{
		title = opt.title;
		yAxis = opt.yAxis;
		xAxis = opt.xAxis;
		stype = opt.stype;
		stypeext = opt.stypeext;
		width=opt.width;
		heigh=opt.heigh;

		lineWidth = opt.lineWidth;
		intervalsext = opt.intervalsext;
		more = opt.more;

		return *this;
	}
};

struct GGraph
{
	// TypeOfGraph
	size_t type;

	// data
	std::string data;

	// option
	std::string option;

	// Google chart option
	GCoptions opt;
};

/////////////////// Constants strings usefull to construct the HTML page //////////

const std::string begin_data ="<html>\n\
  <head>\n\
    <script type=\"text/javascript\" src=\"https://www.gstatic.com/charts/loader.js\"></script>\n\
    <script type=\"text/javascript\">\n\
      google.charts.load('current', {'packages':['corechart']});\n\
      google.charts.setOnLoadCallback(drawVisualization);\n\
\n\
\n\
      function drawVisualization() {\n";

const std::string end_data="]);\n\n";

const std::string begin_div = "}</script>\n\
</head>\n\
<body>\n";

const std::string div_end = "</body>\n\
</html>\n";

/////////////////////////////////////////////////////////////////////

/*! \brief Small class to produce graph with Google chart in HTML
 *
 * This Class can produce several graph using google chart
 *
 * ### Create Histogram graph
 *
 * \image html g_graph_hist.jpg
 *
 * This code produce the graph above
 *
 * \snippet Plot_unit_tests.hpp Producing an Histogram graph
 *
 * ### Create Lines
 *
 * \image html g_graph_plot2.jpg
 *
 * This code produce the graph above
 *
 * \snippet Plot_unit_tests.hpp Producing lines
 *
 * ### Create lines with different styles
 *
 * \image html g_graph_plot.jpg
 *
 * This code produce the graph above
 *
 * \snippet Plot_unit_tests.hpp Producing lines graph with style
 *
 */
class GoogleChart
{
	// set of graphs
	openfpm::vector<GGraph> set_of_graphs;

	// set inject HTML;
	openfpm::vector<std::string> injectHTML;

	/*! \brief Given X and Y vector return the string representing the data section of the Google Chart
	 *
	 * \param X vector
	 * \param Y vector
	 * \param i counter
	 *
	 * \return string with the data section
	 *
	 */
	template<typename X, typename Y> std::string get_points_plot_data(const openfpm::vector<X> & x, const openfpm::vector<Y> & y, const openfpm::vector<std::string> & yn, const GCoptions & opt, size_t i)
	{
		std::stringstream data;

		size_t interval = 0;

		// we require that the number of x elements are the same as y elements

		if (x.size() != y.size())
			std::cerr << "Error: " << __FILE__ << ":" << __LINE__ << " vector x and the vector y must have the same number of elements " << x.size() << "!=" << y.size() << "\n";

		// Google chart visualization
        data << "var data" << i << " = new google.visualization.DataTable();\n";
		if (std::is_same<X,typename std::string>::value == true)
			data << "data" << i << ".addColumn("  << "'string'" << "," << "'" << opt.xAxis <<"');\n";
		else
			data << "data" << i << ".addColumn("  << "'number'" << "," << "'" << opt.xAxis <<"');\n";

        for (size_t j = 0 ; j < y.last().size() ; j++)
        {
        	if (yn.get(j) == std::string("interval"))
        	{
        		data << "data" << i << ".addColumn({id:'i" << interval/2 << "', type:'number', role:'interval'});\n";
        		interval++;
        	}
        	else
        		data << "data" << i << ".addColumn("  << "'number'" << "," << "'" << yn.get(j) <<"');\n";
        }

        data << "data" << i << ".addRows([\n";
        for (size_t i = 0 ; i < y.size() ; i++)
        {

        	for (size_t j = 0 ; j < y.get(i).size()+1 ; j++)
        	{
        		// the first is x
        		if (j == 0)
        		{
        			if (std::is_same<X,typename std::string>::value == true)
        				data << "['"  << x.get(i) << "'";
        			else
        				data << "["  << x.get(i);
        		}
        		else
        			data << "," << y.get(i).get(j-1);
        	}
        	data << "],\n";
        }

		return data.str();
	}

	std::string get_colums_bar_option(const GCoptions & opt)
	{
		std::stringstream str;
		str << "title : '" << opt.title << "',\n";
	    str << "vAxis: {title: '" << opt.yAxis <<  "'},\n";
	    str << "hAxis: {title: '" << opt.xAxis << "'},\n";
	    str << "seriesType: '" << opt.stype << "',\n";
	    if (opt.stypeext.size() != 0)
	    	str << "series: " << opt.stypeext << "\n";
	    str << opt.more;

	    return str.str();
	}

	std::string get_points_plot_option(const GCoptions & opt)
	{
		std::stringstream str;
		str << "title : '" << opt.title << "',\n";
	    str << "vAxis: {title: '" << opt.yAxis <<  "'},\n";
	    str << "hAxis: {title: '" << opt.xAxis << "'},\n";
	    str << "curveType: 'function',\n";

        str << "lineWidth: " << opt.lineWidth << ",\n";
        if (opt.intervalsext.size() != 0)
        	str << "intervals: " << opt.intervalsext << ",\n";
        else
        	str << "intervals: " << "{ 'style':'area' }" << ",\n";

        if (opt.intervalext.size() != 0)
        	str << "interval: " << opt.intervalext << "\n";

        str << opt.more;

	    return str.str();
	}

	/*! \brief Add a graph data variable
	 *
	 * \param of file out
	 * \param i id
	 * \param data string
	 *
	 */
	void addData(std::ofstream & of, size_t i, const std::string & data)
	{

		of << data;
		of << "]);\n";
	}

	/*! \brief Add an option data variable
	 *
	 * \param of file out
	 * \param i id
	 * \param opt string
	 *
	 */
	void addOption(std::ofstream & of, size_t i, const std::string & opt)
	{
		of << "var options";
		of << i;
		of << "= {\n";
		of << opt;
		of << "};\n";
	}

	/*! \brief Add a draw div section
	 *
	 * \param of file out
	 * \param i id
	 *
	 */
	void addDrawDiv(std::ofstream & of, size_t i)
	{
		of << "var chart = new google.visualization.ComboChart(document.getElementById('chart_div";
		of << i;
		of << "'));chart.draw(data";
		of << i;
		of << ", options";
		of << i;
		of << ");\n";
	}

	/*! \brief Add a div section
	 *
	 * \param i id
	 * \param gc GoogleChart option
	 *
	 */
	void addDiv(std::ofstream & of, size_t i, const GCoptions & gc)
	{
		of << "<div id=\"chart_div";
		of << i;
		of << "\" style=\"width: ";
		of << gc.width;
		of << "px; height: ";
		of << gc.heigh;
		of << "px;\"></div>\n";
	}

public:

	GoogleChart()
	{
		injectHTML.add();
	}

	/*! \brief Add an histogram graph
	 *
	 * \param y A vector of vectors the size of y indicate how many values we have on x
	 *          each x value can have multiple values or datasets
	 *
	 */
	template<typename Y> void AddHistGraph(openfpm::vector<Y> & y)
	{
		openfpm::vector<std::string> x;
		x.resize(y.size());

		AddHistGraph<std::string,Y>(x,y);
	}

	/*! \brief Add an histogram graph
	 *
	 * \param y A vector of vectors the size of y indicate how many values we have on x
	 *          each x value can have multiple values or datasets
	 *
	 * \param x Give a name or number to each colums. Can be a string or a number
	 *
	 */
	template<typename X, typename Y> void AddHistGraph(openfpm::vector<X> & x, openfpm::vector<Y> & y)
	{
		GCoptions opt;

		openfpm::vector<std::string> yn;

		if (y.size() != 0)
			yn.resize(y.get(0).size());

		AddHistGraph<X,Y,std::string>(x,y,yn,opt);
	}

	/*! \brief Add an histogram graph
	 *
	 * \param y A vector of vectors the size of y indicate how many values we have on x
	 *          each x value can have multiple values or datasets
	 *
	 * \param x Give a name or number to each colums. Can be a string or a number
	 *
	 * \param yn Give a name to each dataset
	 *
	 */
	template<typename X, typename Y, typename Yn> void AddHistGraph(openfpm::vector<X> & x, openfpm::vector<Y> & y, openfpm::vector<Yn> & yn)
	{
		GCoptions opt;

		AddHistGraph(x,y,yn,opt);
	}

	/*! \brief Add an histogram graph
	 *
	 * \param y A vector of vectors the size of y indicate how many values we have on x
	 *          each x value can have multiple values or datasets
	 *
	 * \param x Give a name or number to each colums. Can be a string or a number
	 *
	 * \param yn Give a name to each dataset
	 *
	 * \param opt Graph options
	 *
	 */
	template<typename X, typename Y, typename Yn> void AddHistGraph(openfpm::vector<X> & x, openfpm::vector<Y> & y, openfpm::vector<Yn> & yn , const GCoptions & opt)
	{
		set_of_graphs.add();
		injectHTML.add();

		// Check that all the internal vector has the same number of elements

		if (y.size() != 0)
		{
			size_t sz = y.get(0).size();
			for (size_t i = 0; i < y.size() ; i++)
			{
				if (y.get(i).size() != sz)
					std::cerr << __FILE__ << ":" << __LINE__ << " error all the elements in the y vector must have the same numbers, element " << i << ": " << y.get(i).size() << " " << " mismatch the numbers of elements at 0: " << sz << "/n";
			}
		}

		set_of_graphs.last().type = GGRAPH_COLUMS;
		set_of_graphs.last().data = get_points_plot_data(x,y,yn,opt,set_of_graphs.size()-1);
		set_of_graphs.last().option = get_colums_bar_option(opt);
		set_of_graphs.last().opt = opt;
	}

	/*! \brief Add a simple lines graph
	 *
	 * \param y A vector of vectors of values. The size of y indicate how many x values
	 *          we have, while the internal vector can store multiple realizations,
	 *          or min and max, for error bar
	 *
	 * \param x Give a name or number to each x value, so can be a string or a number
	 *
	 * \param opt Graph options
	 *
	 */
	template<typename X, typename Y> void AddLinesGraph(openfpm::vector<X> & x, openfpm::vector<Y> & y , const GCoptions & opt)
	{
		openfpm::vector<std::string> yn;

		if (y.size() == 0)
		{
			std::cerr << "Error: " << __FILE__ << ":" << __LINE__ << " vector y must be filled";
			return;
		}

		for (size_t i = 0 ; i < y.last().size() ; i++)
			yn.add(std::string("line") + std::to_string(i));

		AddLinesGraph(x,y,yn,opt);
	}

	/*! \brief Add a simple plot graph
	 *
	 * \param y A vector of vector of values (numbers) the size of y indicate how many x values
	 *          or colums we have, while the internal vector store multiple lines,
	 *          or error bars
	 *
	 * \param x Give a name or number to each colums, so can be a string or a number
	 *
	 * \param yn Give a name to each line, or specify an error bar
	 *
	 * \param opt Graph options
	 *
	 */
	template<typename X, typename Y> void AddLinesGraph(openfpm::vector<X> & x, openfpm::vector<Y> & y , const openfpm::vector<std::string> & yn, const GCoptions & opt)
	{
		if (y.size() == 0)
		{
			std::cerr << "Error: " << __FILE__ << ":" << __LINE__ << " vector y must be filled\n";
			return;
		}

		set_of_graphs.add();
		injectHTML.add();

		// Check that all the internal vectors has the same number of elements

		if (y.size() != 0)
		{
			size_t sz = y.get(0).size();
			for (size_t i = 0; i < y.size() ; i++)
			{
				if (y.get(i).size() != sz)
					std::cerr << __FILE__ << ":" << __LINE__ << " error all the elements in the y vector must have the same numbers, element " << i << ": " << y.get(i).size() << " " << " mismatch the numbers of elements at 0: " << sz << "/n";
			}
		}

		set_of_graphs.last().type = GGRAPH_POINTS;
		set_of_graphs.last().data = get_points_plot_data(x,y,yn,opt,set_of_graphs.size()-1);
		set_of_graphs.last().option = get_points_plot_option(opt);
		set_of_graphs.last().opt = opt;
	}

	/*! \brief Add HTML text
	 *
	 * \param html add html text in the page
	 *
	 */
	void addHTML(const std::string & html)
	{
		injectHTML.last() = html;
	}

	/*! \brief It write the graphs on file in html format using Google charts
	 *
	 * \param file output file
	 *
	 */
	void write(std::string file)
	{
		// Open a file

		std::ofstream of(file);

		// Check if the file is open
		if (of.is_open() == false)
		{std::cerr << "Error cannot create the HTML file: " + file + "\n";}

		// write the file

		of << begin_data;

		for (size_t i = 0 ; i < set_of_graphs.size() ; i++)
			addData(of,i,set_of_graphs.get(i).data);

		for (size_t i = 0 ; i < set_of_graphs.size() ; i++)
			addOption(of,i,set_of_graphs.get(i).option);

		for (size_t i = 0 ; i < set_of_graphs.size() ; i++)
			addDrawDiv(of,i);

		of << begin_div;

		of << injectHTML.get(0);

		for (size_t i = 0 ; i < set_of_graphs.size() ; i++)
		{
			addDiv(of,i,set_of_graphs.get(i).opt);
			of << injectHTML.get(i+1);
		}

		of << div_end;

		of.close();
	}
};

#endif /* OPENFPM_DATA_SRC_PLOT_GOOGLECHART_HPP_ */
