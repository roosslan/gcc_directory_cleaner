/* Пример настроек. Необходимо скопировать в sensitive_data.h и заменить значения на свои.
   sensitive_data.h в репозиторий не добавляется (см. .gitignore). */
#pragma once

#include <string>

/* Директории первого уровня в /xchange, которые xrcleaner не очищает */
std::string const my_directory = "my_directory";
std::string const boss_directory = "boss_directory";
/* Очищается отдельно программой studcleaner */
std::string const exclude_directory = "students";

/* Получатель отчёта */
std::string const mail_to = "admin@example.com";
