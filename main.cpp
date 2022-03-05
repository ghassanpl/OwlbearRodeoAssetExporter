#include <iostream>
#include <fstream>
#include <format>
#include <filesystem>
#include <nlohmann/json.hpp>

#include "mmap.h"
#include "External\Turbo-Base64\turbob64.h"

using namespace std;
using namespace std::filesystem;
using nlohmann::json;

int main(int argc, const char** argv)
{
	if (argc < 2)
	{
		path p = argv[0];
		cout << "Usage: " << p.filename().string() << " <filename.owlbear>\n";
		return 1;
	}

	path p = absolute(argv[1]);
	if (!is_regular_file(p))
	{
		cout << "ERROR: " << p.filename().string() << " is not a file\n";
		return 1;
	}

	path output_directory = p.parent_path();

	try
	{
		auto owlbear_file = ghassanpl::make_mmap_source(p);
		auto owlbear_json = json::parse(owlbear_file);
		auto const& data_array = owlbear_json["data"]["data"];

		json const* asset_array = {};
		json const* map_array = {};
		for (auto& d : data_array)
		{
			if (d["tableName"] == "maps")
				map_array = &d["rows"];
			else if (d["tableName"] == "assets")
				asset_array = &d["rows"];
		}

		if (!asset_array || !map_array)
		{
			cout << "ERROR: " << p.filename().string() << ": no maps or assets in file\n";
			return 1;
		}

		map<string, string> map_name_to_image_id;
		for (auto& map : *map_array)
		{
			if (!map["file"].is_null())
			{
				auto name = string{ map["name"] } + ".json";
				ofstream output{ output_directory / name };
				output << map.dump(2);

				map_name_to_image_id[map["name"]] = map["file"];
			}
			else {
				cout << "NOTE: map " << string{ map["name"] } << " does not have a file associated with it\n";
			}
		}

		for (auto& [name, file_id] : map_name_to_image_id)
		{
			for (auto& asset : *asset_array)
			{
				if (asset["id"] == file_id)
				{
					auto const& b64 = asset["file"]["buffer"].get_ref<std::string const&>();
					auto filename = name + "." + string{ asset["mime"] }.substr(6);

					cout << "Outputting " << filename << "\n";

					auto len = tb64declen((const unsigned char*)b64.data(), b64.size());
					vector<uint8_t> output_buffer;
					output_buffer.resize(len);
					tb64dec((const unsigned char*)b64.data(), b64.size(), output_buffer.data());

					ofstream output{ output_directory / filename, ios::binary };
					output.write((const char*)output_buffer.data(), output_buffer.size());
				}
			}
		}
	}
	catch (exception const& e)
	{
		cout << "ERROR: " << p.filename().string() << ": " << e.what() << "\n";
		return 1;
	}
}