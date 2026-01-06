HA_VERSION=`cat .HA_VERSION`
COMMIT_DATE=$(date +'%d-%m-%Y %H:%M:%S')
COMMIT_MESSAGE="Home Assistant Commit - [$HA_VERSION]: $COMMIT_DATE"

echo "$COMMIT_MESSAGE"

git config --global user.name "your_name"
git config --global user.email "your_email@your_email_provider.com"
git add .
git commit -m "$COMMIT_MESSAGE"
git push
