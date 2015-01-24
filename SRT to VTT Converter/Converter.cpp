/**
 * @file Converter.cpp
 * Implementation for the Converter class.
 *
 * @author Nathan Woltman
 * @copyright 2014-2015 Nathan Woltman
 * @license MIT https://github.com/woollybogger/srt-to-vtt-cl/blob/master/LICENSE.txt
 */

#include <codecvt>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <regex>
#include "Converter.h"
#include "Utils.h"

#if defined(_WIN32) || defined(WIN32)
#define DIR_SEPARATOR "\\"
#else
#define DIR_SEPARATOR "/"
#endif

using namespace std;


Converter::Converter(int timeOffsetMs, const string& outputDir, bool quiet)
{
	_timeOffsetMs = timeOffsetMs;
	_outputDir = outputDir;
	_quiet = quiet;

	// Strip trailing slashes from the output directory's path
	Utils::rtrim(_outputDir, '/');
	Utils::rtrim(_outputDir, '\\');
}

int Converter::convertDirectory(string& dirpath, bool recursive)
{
	// Strip trailing slashes from the directory's path
	Utils::rtrim(dirpath, '/');
	Utils::rtrim(dirpath, '\\');

	print("Searching for files to convert in: " + dirpath);

	DIR *dir = opendir(dirpath.c_str());
	if (dir == NULL) {
		cerr << "Could not read directory \"" << dirpath << "\"" << endl;
		return 1;
	}

	int nRetCodes = 0;

	// Loop through all items in the directory
	for (;;)
	{
		struct dirent *ent = readdir(dir);

		if (ent == NULL) break;

		string item = ent->d_name;
		string ext = item.substr(item.find_last_of(".") + 1);
		switch (ent->d_type) {
			case DT_LNK:
			case DT_REG:
				// If the file is a .srt file, convert it
				if (ext == "srt" || ext == "SRT") {
					nRetCodes += convertFile(dirpath + DIR_SEPARATOR + item);
				}
				break;

			case DT_DIR:
				// If recursive, search subdirectories
				if (recursive && item != "." && item != "..") {
					string subdir = dirpath + DIR_SEPARATOR + item;
					nRetCodes += convertDirectory(subdir, recursive);
				}
				break;

			default:
				break;
		}
	}

	closedir(dir);

	return nRetCodes ? 1 : 0;
}

int Converter::convertFile(string filepath)
{
	// Determine path of the output file
	string outpath = regex_replace(filepath, regex("\\.srt$", regex_constants::icase), string("")) + ".vtt";

	if (!_outputDir.empty()) { // The user specified an output directory
		if (!Utils::isDir(_outputDir)) {
			print("Creating directory: " + _outputDir);
			system(string("mkdir \"" + _outputDir + "\"").c_str());
		}
		outpath = _outputDir + DIR_SEPARATOR + outpath.substr(outpath.find_last_of(DIR_SEPARATOR) + 1);
	}

	print("Converting file: " + filepath + " => " + outpath);
	
	try {
		wregex rgxDialogNumber(L"\\d+");
		wregex rgxTimeFrame(L"(\\d\\d:\\d\\d:\\d\\d,\\d{3}) --> (\\d\\d:\\d\\d:\\d\\d,\\d{3})");

		wifstream infile(filepath);
		Utils::formatInStream(infile, filepath);

		wofstream outfile(outpath);
		outfile.imbue(locale(outfile.getloc(), new codecvt_utf8<wchar_t>));

		// Write mandatory starting for the WebVTT file
		outfile << "WEBVTT" << endl << endl;

		for (;;)
		{
			wstring sLine;

			if (!getline(infile, sLine)) break;

			// Ignore dialog number lines
			if (regex_match(sLine, rgxDialogNumber))
				continue;

			wsmatch match;
			regex_match(sLine, match, rgxTimeFrame);
			if (!match.empty()) {
				if (_timeOffsetMs != 0) {
					// Extract the times in milliseconds from the time frame line
					int msStartTime = timeStringToMs(match[1]);
					int msEndTime = timeStringToMs(match[2]);

					// Modify the time with the offset, making sure the time gets set to 0 if it is going to be negative
					msStartTime += _timeOffsetMs;
					msEndTime += _timeOffsetMs;
					if (msStartTime < 0) msStartTime = 0;
					if (msEndTime < 0) msEndTime = 0;

					// Construct the new time frame line
					sLine = msToVttTimeString(msStartTime) + L" --> " + msToVttTimeString(msEndTime);
				} else {
					// Simply replace the commas in the time with a period
					sLine = Utils::wstr_replace(sLine, L",", L".");
				}
			} else {
				// HTML-encode the text so it is displayed properly by browsers
				htmlEncodeUtf8(sLine);
			}

			outfile << sLine << endl; // Output the line to the new file
		}
	}
	catch (exception &e) {
		cerr << "An error occurred converting \"" << filepath << "\":" << endl << e.what() << endl;
		return 1;
	}

	print("Done!");
	return 0;
}

void Converter::htmlEncodeUtf8(wstring& str)
{
	// HTML-encode certain UTF-8 characters in the ANSI range
	for (size_t i = 0; i < str.length(); i++)
	{
		if (160 <= str[i] && str[i] <= 255) {
			wstring replacement = L"&#" + to_wstring((unsigned int)str[i]) + L";";
			str.replace(i, 1, replacement);
			i += replacement.length() - 1;
		}
	}
}

int Converter::timeStringToMs(const wstring& time)
{
	// Time format: hh:mm:ss,### (where # = ms)
	int hours = stoi(time.substr(0, 2));
	int minutes = stoi(time.substr(3, 2));
	int seconds = stoi(time.substr(6, 2));
	int milliseconds = stoi(time.substr(9));

	return hours * 3600000 + minutes * 60000 + seconds * 1000 + milliseconds;
}

wstring Converter::msToVttTimeString(int ms)
{
	int hours = ms / 3600000;
	ms -= hours * 3600000;

	int minutes = ms / 60000;
	ms -= minutes * 60000;

	int seconds = ms / 1000;
	ms -= seconds * 1000;

	return (hours < 10 ? L"0" : L"") + to_wstring(hours)
		+ L":" + (minutes < 10 ? L"0" : L"") + to_wstring(minutes)
		+ L":" + (seconds < 10 ? L"0" : L"") + to_wstring(seconds)
		+ L"." + (ms < 100 ? L"0" : L"") + (ms < 10 ? L"0" : L"") + to_wstring(ms);
}

void Converter::print(string info)
{
	if (_quiet) return;

	cout << info << endl;
}
