#include <iostream>
#include <fstream>
#include <stdio.h>
#include <filesystem>
#include <chrono>
#include <algorithm>
#include <string>
#include <regex>

namespace fs = std::filesystem;
namespace ch = std::chrono;

void clr_xchange7days(const std::string& directory_to_clean) {
    auto now = ch::system_clock::now();
    auto seven_days = ch::hours(24 * 7);

    for (const auto& entry : fs::recursive_directory_iterator(directory_to_clean)) {
    try {
         auto lwt = fs::last_write_time(entry);
         /* Convert file_time_type to system_clock::time_point */
         auto sctp = ch::time_point_cast<ch::system_clock::duration>(
         lwt - fs::file_time_type::clock::now() + ch::system_clock::now());
									            
         if (now - sctp > seven_days) {
	    if ( (entry.path().u8string().find("/xchange/Ибрагимов") != std::string::npos) || (entry.path().u8string().find("/xchange/Козырев") != std::string::npos)
			  || (entry.path().u8string().find("/xchange/#recycle") != std::string::npos) || (entry.path().u8string().find("@eaDir") != std::string::npos)  ) { /* @eaDir - NAS "extended attributes directory" */ }
	    else {
	    	std::string target_f = std::regex_replace(entry.path().u8string(), std::regex("/xchange/"), "/xchange/#recycle/");

		fs::path students_directory_as_root(std::regex_replace(entry.path().u8string(), std::regex("/xchange/00_Студенты"), "/"));
		fs::path p(std::regex_replace(students_directory_as_root.u8string(), std::regex("/xchange"), "/"));

		std::cout << "rootname:"  << p.root_name() << "\n";
		std::cout << "rootdir:" << p.root_directory() << "\n";
		std::cout << "rootpath:" << p.root_path() << "\n";	
		std::cout << "parent:" << p.parent_path() << "\n";
		std::cout << "lexically:" << p.lexically_relative(*p.begin()) << "\n";

		fs::path directory_w_fn(target_f);
                if (fs::is_directory(entry.path())) { 
			fs::create_directories(directory_w_fn); 
			
		}
		else {
	        	// std::cout << target_f << "\n";

			/* Copying file 
			std::ifstream ifs(entry.path().u8string(), std::ios::in | std::ios::binary);
			std::ofstream ofs(target_f, std::ios::out | std::ios::binary);
			ofs << ifs.rdbuf();
			/* Deleting old file 
			remove(entry.path().u8string().c_str());
			/**/
		}
	    }
         }
     } catch (const std::exception& e) {
	     std::cerr << "Error: " << e.what() << std::endl;
     }
   }
}

int main() {
	clr_xchange7days("/xchange");
	system("pwsh /home/rasa/sendmail.ps1");

	return 0;
}

