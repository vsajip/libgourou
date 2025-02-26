/*
  Copyright (c) 2021, Grégory Soutadé

  All rights reserved.
  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:
  
  * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
  * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
  * Neither the name of the copyright holder nor the
    names of its contributors may be used to endorse or promote products
    derived from this software without specific prior written permission.
  
  THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND ANY
  EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE REGENTS AND CONTRIBUTORS BE LIABLE FOR ANY
  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <getopt.h>
#include <libgen.h>

#include <iostream>
#include <algorithm>

#include <libgourou.h>
#include <libgourou_common.h>

#include "drmprocessorclientimpl.h"
#include "utils_common.h"

static const char* deviceFile     = "device.xml";
static const char* activationFile = "activation.xml";
static const char* devicekeyFile  = "devicesalt";
static const char* acsmFile       = 0;
static       bool  exportPrivateKey = false;
static const char* outputFile     = 0;
static const char* outputDir      = 0;
static       bool  resume         = false;
static       bool  notify         = true;


class ACSMDownloader
{
public:
    
    int run()
    {
	int ret = 0;
	try
	{
	    gourou::DRMProcessor processor(&client, deviceFile, activationFile, devicekeyFile);
	    gourou::User* user = processor.getUser();
	    
	    if (exportPrivateKey)
	    {
		std::string filename;
		if (!outputFile)
		    filename = std::string("Adobe_PrivateLicenseKey--") + user->getUsername() + ".der";
		else
		    filename = outputFile;
	    
		if (outputDir)
		{
		    if (!fileExists(outputDir))
			mkpath(outputDir);
		    
		    filename = std::string(outputDir) + "/" + filename;
		}

		processor.exportPrivateLicenseKey(filename);

		std::cout << "Private license key exported to " << filename << std::endl;
	    }
	    else
	    {
		gourou::FulfillmentItem* item = processor.fulfill(acsmFile, notify);

		std::string filename;
		if (!outputFile)
		{
		    filename = item->getMetadata("title");
		    if (filename == "")
			filename = "output";
		    else
		    {
			// Remove invalid characters
			std::replace(filename.begin(), filename.end(), '/', '_');
		    }
		}
		else
		    filename = outputFile;
	    
		if (outputDir)
		{
		    if (!fileExists(outputDir))
			mkpath(outputDir);

		    filename = std::string(outputDir) + "/" + filename;
		}
	    
		gourou::DRMProcessor::ITEM_TYPE type = processor.download(item, filename, resume);

		if (!outputFile)
		{
		    std::string finalName = filename;
		    if (type == gourou::DRMProcessor::ITEM_TYPE::PDF)
			finalName += ".pdf";
		    else
			finalName += ".epub";
		    rename(filename.c_str(), finalName.c_str());
		    filename = finalName;
		}
		std::cout << "Created " << filename << std::endl;

		serializeLoanToken(item);
	    }
	} catch(std::exception& e)
	{
	    std::cout << e.what() << std::endl;
	    ret = 1;
	}

	return ret;
    }

    void serializeLoanToken(gourou::FulfillmentItem* item)
    {
	gourou::LoanToken* token = item->getLoanToken();

	// No loan token available
	if (!token)
	    return;

	pugi::xml_document doc;

	pugi::xml_node decl = doc.append_child(pugi::node_declaration);
	decl.append_attribute("version") = "1.0";

	pugi::xml_node root = doc.append_child("loanToken");
	gourou::appendTextElem(root, "id",          (*token)["id"]);
	gourou::appendTextElem(root, "operatorURL", (*token)["operatorURL"]);
	gourou::appendTextElem(root, "validity",    (*token)["validity"]);
	gourou::appendTextElem(root, "name",        item->getMetadata("title"));

	char * activationDir = strdup(deviceFile);
	activationDir = dirname(activationDir);
		
	gourou::StringXMLWriter xmlWriter;
	doc.save(xmlWriter, "  ");
	std::string xmlStr = xmlWriter.getResult();

	// Use first bytes of SHA1(id) as filename
	unsigned char sha1[gourou::SHA1_LEN];
	client.digest("SHA1", (unsigned char*)(*token)["id"].c_str(), (*token)["id"].size(), sha1);
	gourou::ByteArray tmp(sha1, sizeof(sha1));
	std::string filenameHex = tmp.toHex();
	std::string filename(filenameHex.c_str(), ID_HASH_SIZE);
	std::string fullPath = std::string(activationDir);
	fullPath += std::string ("/") + std::string(LOANS_DIR);
	mkpath(fullPath.c_str());
	fullPath += filename + std::string(".xml");
	gourou::writeFile(fullPath, xmlStr);

	std::cout << "Loan token serialized into " << fullPath << std::endl;

	free(activationDir);
    }
    
private:
    DRMProcessorClientImpl client;
};	      


static void usage(const char* cmd)
{   
    std::cout << basename((char*)cmd) << " download EPUB file from ACSM request file" << std::endl << std::endl;
    std::cout << "Usage: " << basename((char*)cmd) << " [OPTIONS] file.acsm" << std::endl << std::endl;
    std::cout << "Global Options:" << std::endl;
    std::cout << "  " << "-O|--output-dir"      << "\t"   << "Optional output directory were to put result (default ./)" << std::endl;
    std::cout << "  " << "-o|--output-file"     << "\t"   << "Optional output filename (default <title.(epub|pdf|der)>)" << std::endl;
    std::cout << "  " << "-f|--acsm-file"       << "\t"   << "Backward compatibility: ACSM request file for epub download" << std::endl;
    std::cout << "  " << "-e|--export-private-key"<< "\t" << "Export private key in DER format" << std::endl;
    std::cout << "  " << "-r|--resume"          << "\t\t" << "Try to resume download (in case of previous failure)" << std::endl;
    std::cout << "  " << "-N|--no-notify"       << "\t\t" << "Don't notify server, even if requested" << std::endl;
    std::cout << "  " << "-v|--verbose"         << "\t\t" << "Increase verbosity, can be set multiple times" << std::endl;
    std::cout << "  " << "-V|--version"         << "\t\t" << "Display libgourou version" << std::endl;
    std::cout << "  " << "-h|--help"            << "\t\t" << "This help" << std::endl;

    std::cout << "ADEPT Options:" << std::endl;
    std::cout << "  " << "-D|--adept-directory" << "\t"   << ".adept directory that must contains device.xml, activation.xml and devicesalt" << std::endl;
    std::cout << "  " << "-d|--device-file"     << "\t"   << "device.xml file from eReader" << std::endl;
    std::cout << "  " << "-a|--activation-file" << "\t"   << "activation.xml file from eReader" << std::endl;
    std::cout << "  " << "-k|--device-key-file" << "\t"   << "private device key file (eg devicesalt/devkey.bin) from eReader" << std::endl;

    std::cout << std::endl;
    
    std::cout << "Environment:" << std::endl;
    std::cout << "Device file, activation file and device key file are optionals. If not set, they are looked into :" << std::endl << std::endl;
    std::cout << "  * $ADEPT_DIR environment variable" << std::endl;
    std::cout << "  * /home/<user>/.config/adept" << std::endl;
    std::cout << "  * Current directory" << std::endl;
    std::cout << "  * .adept" << std::endl;
    std::cout << "  * adobe-digital-editions directory" << std::endl;
    std::cout << "  * .adobe-digital-editions directory" << std::endl;
}

int main(int argc, char** argv)
{
    int c, ret = -1;
    std::string _deviceFile, _activationFile, _devicekeyFile;

    const char** files[] = {&devicekeyFile, &deviceFile, &activationFile};
    int verbose = gourou::DRMProcessor::getLogLevel();

    while (1) {
	int option_index = 0;
	static struct option long_options[] = {
	    {"adept-directory",  required_argument, 0,  'D' },
	    {"device-file",      required_argument, 0,  'd' },
	    {"activation-file",  required_argument, 0,  'a' },
	    {"device-key-file",  required_argument, 0,  'k' },
	    {"output-dir",       required_argument, 0,  'O' },
	    {"output-file",      required_argument, 0,  'o' },
	    {"acsm-file",        required_argument, 0,  'f' },
	    {"export-private-key",no_argument,      0,  'e' },
	    {"resume",           no_argument,       0,  'r' },
	    {"no-notify",        no_argument,       0,  'N' },
	    {"verbose",          no_argument,       0,  'v' },
	    {"version",          no_argument,       0,  'V' },
	    {"help",             no_argument,       0,  'h' },
	    {0,                  0,                 0,  0 }
	};

	c = getopt_long(argc, argv, "D:d:a:k:O:o:f:erNvVh",
                        long_options, &option_index);
	if (c == -1)
	    break;

	switch (c) {
	case 'D':
	    _deviceFile = std::string(optarg) + "/device.xml";
	    _activationFile = std::string(optarg) + "/activation.xml";
	    _devicekeyFile = std::string(optarg) + "/devicesalt";
	    deviceFile = _deviceFile.c_str();
	    activationFile = _activationFile.c_str();
	    devicekeyFile = _devicekeyFile.c_str();
	    break;
	case 'd':
	    deviceFile = optarg;
	    break;
	case 'a':
	    activationFile = optarg;
	    break;
	case 'k':
	    devicekeyFile = optarg;
	    break;
	case 'f':
	    acsmFile = optarg;
	    break;
	case 'O':
	    outputDir = optarg;
	    break;
	case 'o':
	    outputFile = optarg;
	    break;
	case 'e':
	    exportPrivateKey = true;
	    break;
	case 'r':
	    resume = true;
	    break;
	case 'N':
	    notify = false;
	    break;
	case 'v':
	    verbose++;
	    break;
	case 'V':
	    version();
	    return 0;
	case 'h':
	    usage(argv[0]);
	    return 0;
	default:
	    usage(argv[0]);
	    return -1;
	}
    }
   
    gourou::DRMProcessor::setLogLevel(verbose);

    if (optind == argc-1)
	acsmFile = argv[optind];

    if ((!acsmFile && !exportPrivateKey) || (outputDir && !outputDir[0]) ||
	(outputFile && !outputFile[0]))
    {
	usage(argv[0]);
	return -1;
    }

    ACSMDownloader downloader;

    int i;
    bool hasErrors = false;
    const char* orig;
    for (i=0; i<(int)ARRAY_SIZE(files); i++)
    {
	orig = *files[i];
	*files[i] = findFile(*files[i]);
	if (!*files[i])
	{
	    std::cout << "Error : " << orig << " doesn't exists, did you activate your device ?" << std::endl;
	    ret = -1;
	    hasErrors = true;
	}
    }

    if (hasErrors)
	goto end;
    
    if (exportPrivateKey)
    {
	if (acsmFile)
	{
	    usage(argv[0]);
	    return -1;
	}
    }
    else
    {
	if (!fileExists(acsmFile))
	{
	    std::cout << "Error : " << acsmFile << " doesn't exists" << std::endl;
	    ret = -1;
	    goto end;
	}
    }
    
    ret = downloader.run();

end:
    for (i=0; i<(int)ARRAY_SIZE(files); i++)
    {
	if (*files[i])
	    free((void*)*files[i]);
    }

    return ret;
}
