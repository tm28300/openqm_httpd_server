# Description

The openqm\_httpd\_server software is a standalone and lightweight http web server. This server allows you to respond to all types of web requests and return a response composed of a header, an httpd status and content corresponding to the Content-Type. The content of the response will most often be in text format. Although OpenQM supports the management of binary streams, we will avoid managing this type of content which will be much better supported by other technologies. Each valid request is handled by a routine. This routine receives all the input information, the url, the method, the GET or POST parameter data, the header data, the client address and some server information. Unlike my first try with openqm\_https\_mod, an Apache httpd module, this server supports:
- Customization of the response when there is an error status. This is interesting in the context of an API when, for example, you have to validate a form and you want to return an error message containing a numerical code, a message and the field to which the error relates. In this case we can for example use the status 400 (bad request) with a json/xml return containing this information.
- The request does not contain the name of the OpenQM routine to call, so less technical information is revealed. It is a configuration file which allows the correspondence between the url/method pair and the routine to be called.

This server is not intended to be called directly from the web, instead it is preferable that requests arrive at a more complete server like Apache httpd. This Apache httpd server will then send the requests to the openqm\_httpd\_server server as a reverse proxy. The advantage of this configuration is that we will benefit from:
- A light and flexible server to respond to requests (openqm\_httpd\_server).
- A robust, configurable and extensible server to respond securely to requests from outside (Apache httpd or other). It is up to this server to take care of the encryption of the connection via https and related standards.
- To have a single host name to contain on the one hand the web application (static html page, application based on a framework such as ReactOS or VueJS, resources such as css, images and fonts) and On the other hand, the API which will interact with our nosql OpenQM/ScarletDME database.
   
# Installation

The software must be compiled and requires the following dependencies:
- gcc
- makefile
- libmicrohttpd12
- libmicrohttpd\_dev
- libconfig9
- libconfig\_dev

Then you need to run **make** command to produce the executable file. After that, you need to manualy copy this file and create the configuration file (see below).

# Use

The server is an executable program which is configured via a configuration file. Once the program is executed in daemon, it will respond to the web requests it receives and call a routine when the url corresponds to one of those configured.

## Configuration

The configuration file has a syntax [libconfig](http://hyperrealm.github.io/libconfig/). At the first level the configuration file must contain:

### httpd

Allows you to define server settings. It is composed of :
- port = Port number to which the server responds.
- env: An array that contains the server environment variables. For example QMCONFIG = the path and name of the OpenQM configuration file alternative to /etc/openqm.conf.

### openqm

Allows you to define OpenQM parameters. It is composed of the single variable:
- account = Name of the OpenQM account in which the routines are cataloged.

### url

Allows you to define the valid URLs, the checks to perform and the routine to call. This is an array of objects. Each object is an URL path (one level at a time) and contains:
- path = Name of a subdirectory level in the url.
- sub\_path: Is an array of objects of the same syntax as this one for the next level of subdirectory.
- pattern = A regular expression which allows you to validate a subdirectory instead of path.
- subr = Name of an OpenQM routine to be called.
- method = An array of strings indicating the http methods that can be used by the request.
- get\_param = An array of strings indicating the list of parameters accepted for GET parameters.

path and pattern cannot be defined at the same time. The url '/' cannot be configured.

## Routines

The routine called to respond to a request requires 13 parameters. The first 10 are used in input:
- auth\_type :
- hostname
- header\_in
- query\_string
- post\_dynarray
- remote\_info : Not implemented. In the future will contain the IP address and port of the client that called the server.
- remote\_user
- method
- uri
- server\_info : IP address, port and protocol of the server in a dynamic array with two attributes linked in multi-values. The first attribute contains the parameter name and the second its value. The implementation is partial and this variable only contains the protocol (http or https).
  
Then 3 input/output parameters:
- http\_output
- http\_status
- header\_out

## Error handling by this software

Before and after calling the routine, the software performs the following checks which can trigger an error with the corresponding http status:
- If the url is '/' or does not correspond to any of those configured, the http status returned is 404 (not found).
- If the url corresponds to one of those configured but there is no routine name, the http status returned is 404 (not found).
- If there is not enough memory to process the request, the http status returned is 500 (internal server error).
- If the method was not authorized in the configuration file, the http status returned is 405 (method not allowed).
- If the request contains a query string the following checks are processed:
    - If in the configuration file there is a get\_param table and the parameter name is missing from the values list, the http status returned is 400 (bad request).
    - If the value of the parameter is greater than 16KB, the http status returned is 400 (bad request).
- If the name of the called host is missing, the http status returned is 400 (bad request).
- If the server cannot connect to OpenQM, the http status returned is 503 (service unavailable).
- After call the routine the http\_status parameter isn't modified, the http status returned is 500 (internal server error).

If the routine returns an empty response and an http error status code or if the software generates an http error status code then the server will generate a default error page. This error page is a minimalist html page containing a title " Error" and containing the error message in English.

When the software encounters a problem generating an http error status and a detailed message in syslog.

# Limitations

The server can only respond to one domain name.

The url '/' can't be used and systematically returns http status 404 (not found).

# TODO

In OpenQM routine if buffer overflow add a method to return the output content in temporary file inside a variable content. Make a generic routine to handle this function.

In the configuration add a **post_param** array to limit the accepted POST parameters (same as get\_param).

Handle receiving files via a POST method and write this to a temporary directory with a temporary, random file name.

Generate an error page depending on the **accept** in request header (html, json or xml), the language in request header and the http status returned.

Write comments in source code.
