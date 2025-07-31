if [ -z $1 ] 
then
  echo "Usage: $0 [configuration file]"
  exit 1
fi

#/usr/local/mysql/bin/mysqld --defaults-file=$1 --debug=d:t:o,/tmp/mysqld.trace --initialize &> ~/logs/mysqld_safe.log &
/usr/local/mysql/bin/mysqld --defaults-file=$1 --initialize &> ~/logs/mysqld_safe.log &

